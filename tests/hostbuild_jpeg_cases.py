## @file
#  @brief Check JPEG validation and transactional asset wrapping.
import contextlib
import io
import os
import subprocess
import tempfile
from pathlib import Path
from unittest import mock

from tests.hostbuild_fixtures import BASELINE_JPEG, PROGRESSIVE_JPEG
from tools import hostbuild


class HostBuildJpegCases:
    def test_embed_jpeg_runs_checked_cupidobj_from_the_seed(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            calls = []
            events = []
            prepare_baseline = hostbuild._prepare_baseline_jpeg
            replace = os.replace

            def run_seed(
                manifest_path,
                working_directory,
                tool_name,
                arguments,
                *,
                timeout,
            ):
                calls.append(
                    (
                        manifest_path,
                        working_directory,
                        tool_name,
                        arguments,
                        timeout,
                    )
                )
                self.assertEqual(tool_name, "cupidobj")
                self.assertEqual(arguments[0], "wrap-jpeg")
                self.assertEqual(arguments[2:4], ["--identity", str(src)])
                frozen = Path(arguments[1])
                candidate = Path(arguments[-1])
                self.assertEqual(frozen.read_bytes(), BASELINE_JPEG)
                self.assertNotEqual(frozen, src)
                self.assertEqual(frozen.parent, candidate.parent)
                self.assertEqual(candidate.parent.parent, root)
                self.assertTrue(
                    candidate.parent.name.startswith(
                        f".{out.name}.embed-jpeg-"
                    )
                )
                events.append("cupidobj")
                candidate.write_bytes(b"checked object")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments],
                    0,
                    "",
                    "",
                )

            def run_oracle(frozen, oracle):
                events.append("python-oracle")
                self.assertEqual(frozen.read_bytes(), BASELINE_JPEG)
                prepare_baseline(frozen, oracle)

            def publish(candidate, destination):
                events.append("publish")
                self.assertEqual(Path(candidate).read_bytes(), b"checked object")
                self.assertEqual(Path(destination), out)
                replace(candidate, destination)

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                    create=True,
                ),
                mock.patch(
                    "tools.hostbuild._prepare_baseline_jpeg",
                    side_effect=run_oracle,
                ),
                mock.patch(
                    "tools.hostbuild.os.replace",
                    side_effect=publish,
                ),
            ):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 0)
            self.assertEqual(len(calls), 1)
            self.assertEqual(
                events,
                ["cupidobj", "python-oracle", "publish"],
            )
            self.assertEqual(out.read_bytes(), b"checked object")

    def test_embed_jpeg_preserves_output_after_checked_seed_failure(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            out.write_bytes(b"existing object")
            calls = []
            diagnostic = io.StringIO()

            def run_seed(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                **_kwargs,
            ):
                calls.append(arguments)
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments],
                    6,
                    "",
                    "wrap failed\n",
                )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                    create=True,
                ),
                mock.patch(
                    "tools.hostbuild._prepare_baseline_jpeg",
                    side_effect=AssertionError(
                        "Python oracle must not run before CupidObj"
                    ),
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(calls[0][0], "wrap-jpeg")
            self.assertIn(
                "checked CupidObj failed with status 6: wrap failed",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_rejects_manifest_drift_before_publication(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            out.write_bytes(b"existing object")
            diagnostic = io.StringIO()

            def run_seed(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                **_kwargs,
            ):
                Path(arguments[-1]).write_bytes(b"checked object")
                manifest.write_bytes(b"changed manifest")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments], 0, "", ""
                )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "checked JPEG inputs changed while wrapping the object",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_rejects_input_drift_before_publication(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            out.write_bytes(b"existing object")
            diagnostic = io.StringIO()

            def run_seed(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                *,
                timeout,
            ):
                self.assertEqual(timeout, 60)
                src.write_bytes(BASELINE_JPEG + b"\x00")
                Path(arguments[-1]).write_bytes(b"checked object")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments],
                    0,
                    "",
                    "",
                )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "checked JPEG inputs changed while wrapping the object",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_wraps_checked_in_bytes_with_original_identity(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            src.write_bytes(BASELINE_JPEG)
            object_tool_commands = []

            def fake_run(args, **kwargs):
                if args[0] == "cupidobj":
                    object_tool_commands.append(args)
                    self.assertEqual(
                        Path(args[2]).read_bytes(),
                        BASELINE_JPEG,
                    )
                    Path(args[-1]).write_bytes(b"object")
                    return subprocess.CompletedProcess(args, 0)
                raise AssertionError(f"unexpected command: {args}")

            with mock.patch(
                "tools.hostbuild.shutil.which",
                side_effect=AssertionError("host converter lookup"),
            ), mock.patch(
                "tools.hostbuild.subprocess.run",
                side_effect=fake_run,
            ):
                hostbuild.embed_jpeg("cupidobj", src, out)

            self.assertEqual(
                object_tool_commands,
                [[
                    "cupidobj",
                    "wrap",
                    str(out) + ".baseline.jpg",
                    "--identity",
                    str(src),
                    "-o",
                    str(out),
                ]],
            )
            self.assertTrue(out.exists())

    def test_embed_jpeg_rejects_progressive_input_without_replacing_output(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(PROGRESSIVE_JPEG)
            out.write_bytes(b"existing object")
            diagnostic = io.StringIO()
            calls = []

            def run_seed(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                **_kwargs,
            ):
                calls.append(arguments)
                self.assertEqual(arguments[0], "wrap-jpeg")
                self.assertEqual(
                    Path(arguments[1]).read_bytes(), PROGRESSIVE_JPEG
                )
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments],
                    1,
                    "",
                    "unsupported progressive JPEG frame; "
                    "check in a baseline SOF0/SOF1 asset\n",
                )

            with (
                mock.patch(
                    "tools.hostbuild.shutil.which",
                    side_effect=AssertionError("host converter lookup"),
                ),
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                ),
                mock.patch(
                    "tools.hostbuild._prepare_baseline_jpeg",
                    side_effect=AssertionError(
                        "Python oracle must not run before CupidObj"
                    ),
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(len(calls), 1)
            self.assertIn(
                "unsupported progressive JPEG frame",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_uses_cupidobj_to_reject_a_malformed_frame(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(
                BASELINE_JPEG.replace(
                    b"\xff\xc0\x00\x0b",
                    b"\xff\xc0\x00\x08",
                    1,
                )
            )
            out.write_bytes(b"existing object")
            diagnostic = io.StringIO()
            calls = []

            def run_seed(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                **_kwargs,
            ):
                calls.append(arguments)
                self.assertEqual(arguments[0], "wrap-jpeg")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments],
                    1,
                    "",
                    "JPEG frame header has an invalid component table\n",
                )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                ),
                mock.patch(
                    "tools.hostbuild._prepare_baseline_jpeg",
                    side_effect=AssertionError(
                        "Python oracle must not run before CupidObj"
                    ),
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(len(calls), 1)
            self.assertIn(
                "JPEG frame header has an invalid component table",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_rejects_python_oracle_disagreement(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            out.write_bytes(b"existing object")
            diagnostic = io.StringIO()
            calls = []

            def run_seed(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                **_kwargs,
            ):
                calls.append(arguments)
                Path(arguments[-1]).write_bytes(b"checked object")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments], 0, "", ""
                )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                ),
                mock.patch(
                    "tools.hostbuild._prepare_baseline_jpeg",
                    side_effect=hostbuild.EmbedJpegError(
                        "Python rejected the frame"
                    ),
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertEqual(len(calls), 1)
            self.assertIn(
                "checked CupidObj JPEG acceptance differs from the "
                "Python oracle: Python rejected the frame",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_reports_python_oracle_write_failure(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            out.write_bytes(b"existing object")
            diagnostic = io.StringIO()
            write_bytes = Path.write_bytes

            def run_seed(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                **_kwargs,
            ):
                Path(arguments[-1]).write_bytes(b"checked object")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments], 0, "", ""
                )

            def guarded_write(path, payload):
                if path.name == "asset.python-oracle.jpg":
                    raise OSError("oracle copy denied")
                return write_bytes(path, payload)

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                ),
                mock.patch(
                    "pathlib.Path.write_bytes",
                    new=guarded_write,
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "Python JPEG oracle could not write its private copy",
                diagnostic.getvalue(),
            )
            self.assertNotIn(
                "JPEG acceptance differs",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_rejects_python_oracle_byte_drift(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            out.write_bytes(b"existing object")
            diagnostic = io.StringIO()

            def run_seed(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                **_kwargs,
            ):
                Path(arguments[-1]).write_bytes(b"checked object")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments], 0, "", ""
                )

            def alter_oracle(_frozen, oracle):
                oracle.write_bytes(BASELINE_JPEG + b"\x00")

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                ),
                mock.patch(
                    "tools.hostbuild._prepare_baseline_jpeg",
                    side_effect=alter_oracle,
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "Python JPEG oracle changed the frozen input bytes",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_rejects_missing_checked_candidate(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            out.write_bytes(b"existing object")
            diagnostic = io.StringIO()
            success = subprocess.CompletedProcess(
                ["cupidobj", "wrap-jpeg"], 0, "", ""
            )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    return_value=success,
                ),
                mock.patch(
                    "tools.hostbuild._prepare_baseline_jpeg",
                    side_effect=AssertionError(
                        "Python oracle must not inspect a missing candidate"
                    ),
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "checked CupidObj reported success without a regular object",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_rejects_a_symbolic_checked_candidate(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            out.write_bytes(b"existing object")
            diagnostic = io.StringIO()

            def run_seed(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                **_kwargs,
            ):
                candidate = Path(arguments[-1])
                target = candidate.parent / "candidate-target.o"
                target.write_bytes(b"checked object")
                try:
                    candidate.symlink_to(target)
                except OSError as error:
                    self.skipTest(
                        f"symbolic links are unavailable: {error}"
                    )
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments], 0, "", ""
                )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                ),
                mock.patch(
                    "tools.hostbuild._prepare_baseline_jpeg",
                    side_effect=AssertionError(
                        "Python oracle must not inspect a linked candidate"
                    ),
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "checked CupidObj reported success without a regular object",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

    def test_embed_jpeg_rejects_unsafe_output_paths_before_cupidobj(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            directory = root / "directory.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            directory.mkdir()

            cases = (
                (src, "may not replace an input"),
                (manifest, "may not replace an input"),
                (directory, "embedded JPEG output is not a file"),
            )
            for output, expected in cases:
                with self.subTest(output=output.name):
                    diagnostic = io.StringIO()
                    with (
                        mock.patch(
                            "tools.hostbuild.run_seed_tool",
                            side_effect=AssertionError(
                                "CupidObj must not run for an unsafe path"
                            ),
                        ),
                        contextlib.redirect_stderr(diagnostic),
                    ):
                        status = hostbuild.main(
                            [
                                "embed-jpeg",
                                "--seed-manifest",
                                str(manifest),
                                str(src),
                                str(output),
                            ]
                        )
                    self.assertEqual(status, 1)
                    self.assertIn(expected, diagnostic.getvalue())

    def test_embed_jpeg_rejects_missing_and_non_file_inputs_before_cupidobj(
        self,
    ):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            missing = root / "missing.jpg"
            directory = root / "directory.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            directory.mkdir()

            cases = (
                (missing, "checked JPEG path cannot be resolved"),
                (directory, "JPEG input is not a file"),
            )
            for source, expected in cases:
                with self.subTest(source=source.name):
                    out.write_bytes(b"existing object")
                    diagnostic = io.StringIO()
                    with (
                        mock.patch(
                            "tools.hostbuild.run_seed_tool",
                            side_effect=AssertionError(
                                "CupidObj must not run for an unsafe input"
                            ),
                        ),
                        contextlib.redirect_stderr(diagnostic),
                    ):
                        status = hostbuild.main(
                            [
                                "embed-jpeg",
                                "--seed-manifest",
                                str(manifest),
                                str(source),
                                str(out),
                            ]
                        )
                    self.assertEqual(status, 1)
                    self.assertIn(expected, diagnostic.getvalue())
                    self.assertEqual(
                        out.read_bytes(), b"existing object"
                    )

    def test_embed_jpeg_rejects_a_symbolic_output_before_cupidobj(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            target = root / "existing.o"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            target.write_bytes(b"existing object")
            try:
                out.symlink_to(target)
            except OSError as error:
                self.skipTest(f"symbolic links are unavailable: {error}")
            diagnostic = io.StringIO()

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=AssertionError(
                        "CupidObj must not run for a linked output"
                    ),
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "embedded JPEG output may not be a symlink",
                diagnostic.getvalue(),
            )
            self.assertEqual(target.read_bytes(), b"existing object")

    def test_embed_jpeg_preserves_output_after_publication_failure(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            manifest = root / "manifest.json"
            src = root / "photo.jpg"
            out = root / "photo.jpg.o"
            manifest.write_bytes(b"checked manifest")
            src.write_bytes(BASELINE_JPEG)
            out.write_bytes(b"existing object")
            diagnostic = io.StringIO()

            def run_seed(
                _manifest,
                _working_directory,
                _tool_name,
                arguments,
                **_kwargs,
            ):
                self.assertEqual(arguments[0], "wrap-jpeg")
                Path(arguments[-1]).write_bytes(b"checked object")
                return subprocess.CompletedProcess(
                    ["cupidobj", *arguments], 0, "", ""
                )

            with (
                mock.patch(
                    "tools.hostbuild.run_seed_tool",
                    side_effect=run_seed,
                ),
                mock.patch(
                    "tools.hostbuild.os.replace",
                    side_effect=OSError("publication denied"),
                ),
                contextlib.redirect_stderr(diagnostic),
            ):
                status = hostbuild.main(
                    [
                        "embed-jpeg",
                        "--seed-manifest",
                        str(manifest),
                        str(src),
                        str(out),
                    ]
                )

            self.assertEqual(status, 1)
            self.assertIn(
                "embedded JPEG object could not be published: "
                "publication denied",
                diagnostic.getvalue(),
            )
            self.assertEqual(out.read_bytes(), b"existing object")

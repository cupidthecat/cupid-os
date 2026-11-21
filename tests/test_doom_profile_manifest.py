## @file
#  @brief Verify the Doom input manifest and its guarded publication.
import hashlib
import inspect
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import threading
import unittest
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from unittest import mock

from tests.cupidc_kernel_compile_fixtures import REPO_ROOT, SEED_MANIFEST
from tests.doom_profile_fixtures import DoomProfileFixture
from tools import cupidc_kernel_compile as kernel_compile


PROFILE_SNAPSHOT_MAGIC = b"CUPROF1\0"


def _profile_snapshot_oracle(payload):
    """Decode CUPROF1 without borrowing the production implementation."""
    offset = 0

    def take(size, label):
        nonlocal offset
        end = offset + size
        if end > len(payload):
            raise AssertionError(f"profile snapshot ended inside {label}")
        value = payload[offset:end]
        offset = end
        return value

    def take_u32(label):
        return struct.unpack("<I", take(4, label))[0]

    def take_bytes(label):
        return take(take_u32(f"{label} length"), label)

    def take_text(label):
        return take_bytes(label).decode("ascii")

    magic = take(len(PROFILE_SNAPSHOT_MAGIC), "magic")
    if magic != PROFILE_SNAPSHOT_MAGIC:
        raise AssertionError(f"unexpected profile snapshot magic: {magic!r}")
    schema = take_text("schema")
    profiles = {}
    sources = {}
    for profile_index in range(take_u32("profile count")):
        name = take_text(f"profile {profile_index} name")
        headers = [
            take_text(f"profile {profile_index} header {header_index}")
            for header_index in range(take_u32("header count"))
        ]
        members = [
            take_text(f"profile {profile_index} source {source_index}")
            for source_index in range(take_u32("source count"))
        ]
        profiles[name] = sorted(headers)
        sources[name] = sorted(members)
    captured = {}
    for input_index in range(take_u32("input count")):
        path = take_text(f"input {input_index} path")
        captured[path] = take_bytes(f"input {input_index} bytes")
    if offset != len(payload):
        raise AssertionError("profile snapshot has trailing bytes")
    document = {
        "schema": schema,
        "profiles": dict(sorted(profiles.items())),
        "sources": dict(sorted(sources.items())),
        "inputs": [
            {
                "path": path,
                "bytes": len(contents),
                "sha256": hashlib.sha256(contents).hexdigest(),
            }
            for path, contents in sorted(captured.items())
        ],
    }
    oracle = (json.dumps(document, indent=2, sort_keys=True) + "\n").encode(
        "ascii"
    )
    return document, captured, oracle


class DoomProfileManifestTests(DoomProfileFixture, unittest.TestCase):
    def _write_manifest_with_checked_seed(self, root, output):
        parameters = inspect.signature(
            kernel_compile.write_profile_input_manifest
        ).parameters
        if "manifest" in parameters:
            return kernel_compile.write_profile_input_manifest(
                root,
                output,
                manifest=SEED_MANIFEST,
            )
        return kernel_compile.write_profile_input_manifest(root, output)

    def _checked_manifest_paths(
        self,
        manifest_path,
        working_directory,
        tool_name,
        arguments,
    ):
        self.assertEqual(Path(manifest_path).resolve(), SEED_MANIFEST.resolve())
        self.assertEqual(tool_name, "cupidobj")
        self.assertEqual(len(arguments), 4)
        self.assertEqual(arguments[0], "profile-manifest")
        self.assertEqual(arguments[2], "-o")
        self.assertIsInstance(arguments[1], Path)
        self.assertIsInstance(arguments[3], Path)
        self.assertTrue(arguments[1].is_absolute())
        self.assertTrue(arguments[3].is_absolute())

        def native_path(value):
            path = Path(value)
            if not path.is_absolute():
                path = Path(working_directory) / path
            return path.resolve(strict=False)

        return native_path(arguments[1]), native_path(arguments[3])

    def _profile_lock_path(self, output):
        resolved = output.resolve(strict=False)
        digest = hashlib.sha256(
            str(resolved).casefold().encode("utf-8")
        ).hexdigest()
        return resolved.parent / f".cupid-profile-{digest}.lock"

    def test_profile_input_manifest_records_every_approved_source(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        output = root / "build" / "doom-inputs.json"
        output.parent.mkdir()
        kernel_compile.write_profile_input_manifest(root, output)
        document = json.loads(output.read_text(encoding="utf-8"))
        self.assertEqual(
            document["sources"],
            {
                "doom-compat": list(
                    kernel_compile.APPROVED_DOOM_COMPAT_SOURCES
                ),
                "doom-tree": list(
                    kernel_compile.APPROVED_DOOM_TREE_SOURCES
                ),
            },
        )
        self.assertEqual(
            sum(len(members) for members in document["sources"].values()),
            83,
        )

    def test_profile_input_manifest_rejects_a_legacy_c_source(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        legacy = (
            root
            / "kernel"
            / "doom"
            / "src"
            / "unlisted"
            / "legacy.c"
        )
        legacy.parent.mkdir()
        legacy.write_text("int legacy_doom_source;\n", encoding="utf-8")
        output = root / "build" / "doom-inputs.json"

        with self.assertRaisesRegex(
            kernel_compile.KernelCompileError,
            r"unlisted kernel/doom/src/unlisted/legacy\.c",
        ):
            kernel_compile.write_profile_input_manifest(root, output)
        self.assertFalse(output.exists())

    def test_profile_input_manifest_changes_only_with_the_header_space(self):
        root, _source, header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        output = root / "build" / "doom-inputs.json"
        output.parent.mkdir()
        kernel_compile.write_profile_input_manifest(root, output)
        first = output.read_bytes()
        first_time = output.stat().st_mtime_ns
        kernel_compile.write_profile_input_manifest(root, output)
        self.assertEqual(output.read_bytes(), first)
        self.assertEqual(output.stat().st_mtime_ns, first_time)

        original_header = header.read_bytes()
        header.write_text(
            "#define DOOM_SHADOW 9\n",
            encoding="utf-8",
        )
        kernel_compile.write_profile_input_manifest(root, output)
        self.assertNotEqual(output.read_bytes(), first)

        header.write_bytes(original_header)
        kernel_compile.write_profile_input_manifest(root, output)
        self.assertEqual(output.read_bytes(), first)

        added = root / "kernel" / "doom" / "added.h"
        added.write_text("#define ADDED 1\n", encoding="utf-8")
        kernel_compile.write_profile_input_manifest(root, output)
        changed = output.read_bytes()
        self.assertNotEqual(changed, first)

        added.unlink()
        kernel_compile.write_profile_input_manifest(root, output)
        self.assertEqual(output.read_bytes(), first)

    def test_checked_cupidobj_authors_one_frozen_profile_snapshot(self):
        root, _source, header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        output = root / "build" / "doom-inputs.json"
        output.parent.mkdir()
        snapshots = []
        candidates = []

        def run_checked(
            manifest_path,
            working_directory,
            tool_name,
            arguments,
            **_options,
        ):
            frozen_seed = _options.get("frozen_seed")
            self.assertIsNotNone(frozen_seed)
            self.assertEqual(
                set(frozen_seed.tools),
                {
                    "cupidasm",
                    "cupidbuild",
                    "cupidc",
                    "cupiddis",
                    "cupidld",
                    "cupidobj",
                },
            )
            self.assertTrue(
                all(
                    path.parent.name == "checked-seed"
                    for path in frozen_seed.tools.values()
                )
            )
            self.assertNotEqual(
                frozen_seed.tools["cupidobj"].resolve(),
                SEED_MANIFEST.with_name("cupidobj.elf").resolve(),
            )
            snapshot, candidate = self._checked_manifest_paths(
                manifest_path,
                working_directory,
                tool_name,
                arguments,
            )
            self.assertNotEqual(candidate, output.resolve())
            frozen = snapshot.read_bytes()
            document, captured, oracle = _profile_snapshot_oracle(frozen)
            candidate.write_bytes(oracle)
            self.assertEqual(snapshot.read_bytes(), frozen)
            snapshots.append((frozen, document, captured, oracle))
            candidates.append(candidate)
            return subprocess.CompletedProcess(
                ["cupidobj", "profile-manifest"],
                0,
                "",
                "",
            )

        with mock.patch.object(
            kernel_compile,
            "run_seed_tool",
            side_effect=run_checked,
            create=True,
        ):
            changed = self._write_manifest_with_checked_seed(root, output)

        self.assertTrue(changed)
        self.assertEqual(
            len(snapshots),
            1,
            "the checked CupidObj publisher was not called exactly once",
        )
        frozen, document, captured, oracle = snapshots[0]
        self.assertTrue(frozen.startswith(PROFILE_SNAPSHOT_MAGIC))
        self.assertEqual(document["schema"], "cupid.doom-profile-inputs.v1")
        self.assertEqual(
            document["sources"]["doom-compat"],
            sorted(kernel_compile.APPROVED_DOOM_COMPAT_SOURCES),
        )
        self.assertEqual(
            document["sources"]["doom-tree"],
            sorted(kernel_compile.APPROVED_DOOM_TREE_SOURCES),
        )
        relative_header = header.relative_to(root).as_posix()
        self.assertEqual(captured[relative_header], header.read_bytes())
        self.assertEqual(output.read_bytes(), oracle)
        self.assertTrue(all(not candidate.exists() for candidate in candidates))

    def test_cupidobj_disagreement_keeps_the_old_profile_manifest(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        output = root / "build" / "doom-inputs.json"
        output.parent.mkdir()
        old_payload = b'{"old": true}\n'
        output.write_bytes(old_payload)
        candidates = []

        def disagree(
            manifest_path,
            working_directory,
            tool_name,
            arguments,
            **_options,
        ):
            snapshot, candidate = self._checked_manifest_paths(
                manifest_path,
                working_directory,
                tool_name,
                arguments,
            )
            _profile_snapshot_oracle(snapshot.read_bytes())
            candidate.write_bytes(b'{"schema": "not-the-oracle"}\n')
            candidates.append(candidate)
            return subprocess.CompletedProcess(
                ["cupidobj", "profile-manifest"],
                0,
                "",
                "",
            )

        with mock.patch.object(
            kernel_compile,
            "run_seed_tool",
            side_effect=disagree,
            create=True,
        ):
            with self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                r"(?i)(does not match|differs|parity)",
            ):
                self._write_manifest_with_checked_seed(root, output)

        self.assertEqual(output.read_bytes(), old_payload)
        self.assertEqual(len(candidates), 1)
        self.assertTrue(all(not candidate.exists() for candidate in candidates))

    def test_bad_cupidobj_candidates_keep_the_old_profile_manifest(self):
        cases = (
            ("tool failure", "failed"),
            ("missing candidate", "missing"),
            ("directory candidate", "directory"),
        )
        for label, behavior in cases:
            with self.subTest(label=label):
                root, _source, _header, _seed, _manifest, _output = (
                    self._profile_fixture()
                )
                output = root / "build" / "doom-inputs.json"
                output.parent.mkdir()
                old_payload = f"old manifest for {label}\n".encode("ascii")
                output.write_bytes(old_payload)
                candidates = []

                def run_bad_candidate(
                    manifest_path,
                    working_directory,
                    tool_name,
                    arguments,
                    **_options,
                ):
                    snapshot, candidate = self._checked_manifest_paths(
                        manifest_path,
                        working_directory,
                        tool_name,
                        arguments,
                    )
                    _profile_snapshot_oracle(snapshot.read_bytes())
                    candidates.append(candidate)
                    if candidate.exists():
                        if candidate.is_dir():
                            candidate.rmdir()
                        else:
                            candidate.unlink()
                    if behavior == "failed":
                        candidate.write_bytes(b"partial output")
                        return subprocess.CompletedProcess(
                            ["cupidobj", "profile-manifest"],
                            7,
                            "",
                            "deliberate CupidObj failure",
                        )
                    if behavior == "directory":
                        candidate.mkdir()
                    return subprocess.CompletedProcess(
                        ["cupidobj", "profile-manifest"],
                        0,
                        "",
                        "",
                    )

                with mock.patch.object(
                    kernel_compile,
                    "run_seed_tool",
                    side_effect=run_bad_candidate,
                    create=True,
                ):
                    with self.assertRaisesRegex(
                        kernel_compile.KernelCompileError,
                        r"(?i)(CupidObj|candidate|regular|output)",
                    ):
                        self._write_manifest_with_checked_seed(root, output)

                self.assertEqual(output.read_bytes(), old_payload)
                self.assertEqual(len(candidates), 1)
                self.assertTrue(
                    all(not candidate.exists() for candidate in candidates)
                )

    def test_profile_manifest_rejects_hard_linked_output_and_lock_files(self):
        for linked_path in ("output", "lock"):
            with self.subTest(linked_path=linked_path):
                root, _source, _header, _seed, _manifest, _output = (
                    self._profile_fixture()
                )
                output = root / "build" / "doom-inputs.json"
                output.parent.mkdir()
                victim = root / f"{linked_path}-victim"
                victim.write_bytes(b"victim bytes")
                target = (
                    output
                    if linked_path == "output"
                    else self._profile_lock_path(output)
                )
                os.link(victim, target)

                with self.assertRaisesRegex(
                    kernel_compile.KernelCompileError,
                    r"(?i)(hard link|lock|unavailable)",
                ):
                    kernel_compile.write_profile_input_manifest(root, output)

                self.assertEqual(victim.read_bytes(), b"victim bytes")
                self.assertEqual(victim.stat().st_nlink, 2)

    def test_profile_manifest_rejects_symlinked_output_and_lock_files(self):
        created = 0
        for linked_path in ("output", "lock"):
            with self.subTest(linked_path=linked_path):
                root, _source, _header, _seed, _manifest, _output = (
                    self._profile_fixture()
                )
                output = root / "build" / "doom-inputs.json"
                output.parent.mkdir()
                victim = root / f"{linked_path}-victim"
                victim.write_bytes(b"victim bytes")
                target = (
                    output
                    if linked_path == "output"
                    else self._profile_lock_path(output)
                )
                try:
                    target.symlink_to(victim)
                except OSError:
                    continue
                created += 1

                with self.assertRaisesRegex(
                    kernel_compile.KernelCompileError,
                    r"(?i)(link|junction|lock|unavailable)",
                ):
                    kernel_compile.write_profile_input_manifest(root, output)

                self.assertEqual(victim.read_bytes(), b"victim bytes")
        if created == 0:
            self.skipTest("file symlinks are unavailable")

    def test_checked_cupidobj_candidate_may_not_be_a_link(self):
        for link_kind in ("hard", "symbolic"):
            with self.subTest(link_kind=link_kind):
                root, _source, _header, _seed, _manifest, _output = (
                    self._profile_fixture()
                )
                output = root / "build" / "doom-inputs.json"
                output.parent.mkdir()
                output.write_bytes(b"old manifest\n")
                link_created = []

                def write_linked_candidate(
                    manifest_path,
                    working_directory,
                    tool_name,
                    arguments,
                    **_options,
                ):
                    snapshot, candidate = self._checked_manifest_paths(
                        manifest_path,
                        working_directory,
                        tool_name,
                        arguments,
                    )
                    _document, _captured, oracle = _profile_snapshot_oracle(
                        snapshot.read_bytes()
                    )
                    victim = Path(working_directory) / "linked-candidate.json"
                    victim.write_bytes(oracle)
                    if link_kind == "hard":
                        os.link(victim, candidate)
                    else:
                        try:
                            candidate.symlink_to(victim)
                        except OSError:
                            return subprocess.CompletedProcess(
                                ["cupidobj", "profile-manifest"],
                                9,
                                "",
                                "symlinks unavailable",
                            )
                    link_created.append(True)
                    return subprocess.CompletedProcess(
                        ["cupidobj", "profile-manifest"],
                        0,
                        "",
                        "",
                    )

                with mock.patch.object(
                    kernel_compile,
                    "run_seed_tool",
                    side_effect=write_linked_candidate,
                ):
                    with self.assertRaisesRegex(
                        kernel_compile.KernelCompileError,
                        r"(?i)(CupidObj|hard link|link|failed)",
                    ):
                        self._write_manifest_with_checked_seed(root, output)

                self.assertEqual(output.read_bytes(), b"old manifest\n")
                if link_kind == "hard":
                    self.assertEqual(link_created, [True])

    def test_publish_retry_rechecks_candidate_and_existing_output(self):
        for drift in ("candidate", "output"):
            with self.subTest(drift=drift):
                root, _source, _header, _seed, _manifest, _output = (
                    self._profile_fixture()
                )
                output = root / "build" / "doom-inputs.json"
                output.parent.mkdir()
                output.write_bytes(b"old manifest\n")

                def write_candidate(
                    manifest_path,
                    working_directory,
                    tool_name,
                    arguments,
                    **_options,
                ):
                    snapshot, candidate = self._checked_manifest_paths(
                        manifest_path,
                        working_directory,
                        tool_name,
                        arguments,
                    )
                    _document, _captured, oracle = _profile_snapshot_oracle(
                        snapshot.read_bytes()
                    )
                    candidate.write_bytes(oracle)
                    return subprocess.CompletedProcess(
                        ["cupidobj", "profile-manifest"],
                        0,
                        "",
                        "",
                    )

                def drift_during_replace(source, destination):
                    if drift == "candidate":
                        Path(source).write_bytes(b"late candidate drift")
                    else:
                        Path(destination).write_bytes(b"competing publisher")
                    raise PermissionError("deliberate sharing violation")

                with (
                    mock.patch.object(
                        kernel_compile,
                        "run_seed_tool",
                        side_effect=write_candidate,
                    ),
                    mock.patch.object(
                        kernel_compile.os,
                        "replace",
                        side_effect=drift_during_replace,
                    ),
                ):
                    with self.assertRaisesRegex(
                        kernel_compile.KernelCompileError,
                        r"(?i)(changed|drift|candidate|output)",
                    ):
                        self._write_manifest_with_checked_seed(root, output)

                expected = (
                    b"old manifest\n"
                    if drift == "candidate"
                    else b"competing publisher"
                )
                self.assertEqual(output.read_bytes(), expected)

    def test_profile_output_directory_identity_is_stable(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        directory = root / "build"
        directory.mkdir()
        capture = kernel_compile._capture_profile_directory(
            directory,
            "profile output directory",
        )
        directory.rename(root / "old-build")
        directory.mkdir()

        with self.assertRaisesRegex(
            kernel_compile.KernelCompileError,
            r"(?i)(directory changed|changed while)",
        ):
            kernel_compile._require_profile_directory_unchanged(capture)

    def test_seed_change_before_cupidobj_execution_is_rejected(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        output = root / "build" / "doom-inputs.json"
        output.parent.mkdir()
        output.write_bytes(b"old manifest\n")
        freeze_seed = kernel_compile.freeze_seed_inputs

        def change_frozen_identity(manifest_path, snapshot_directory):
            frozen = freeze_seed(manifest_path, snapshot_directory)
            return mock.Mock(
                manifest_sha256="0" * 64,
                tools=frozen.tools,
            )

        with (
            mock.patch.object(
                kernel_compile,
                "freeze_seed_inputs",
                side_effect=change_frozen_identity,
            ),
            mock.patch.object(kernel_compile, "run_seed_tool") as run_tool,
        ):
            with self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                r"(?i)(seed changed|before CupidObj)",
            ):
                self._write_manifest_with_checked_seed(root, output)

        run_tool.assert_not_called()
        self.assertEqual(output.read_bytes(), b"old manifest\n")

    def test_live_seed_drift_after_cupidobj_preserves_the_old_manifest(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        output = root / "build" / "doom-inputs.json"
        output.parent.mkdir()
        output.write_bytes(b"old manifest\n")
        copied_seed = root / "checked-seed"
        shutil.copytree(SEED_MANIFEST.parent, copied_seed)
        manifest = copied_seed / "manifest.json"

        def mutate_live_seed(
            _manifest_path,
            _working_directory,
            _tool_name,
            arguments,
            **_options,
        ):
            snapshot = Path(arguments[1])
            candidate = Path(arguments[3])
            _document, _captured, oracle = _profile_snapshot_oracle(
                snapshot.read_bytes()
            )
            candidate.write_bytes(oracle)
            image = bytearray((copied_seed / "cupidobj.elf").read_bytes())
            image[-1] ^= 0x01
            (copied_seed / "cupidobj.elf").write_bytes(image)
            return subprocess.CompletedProcess(
                ["cupidobj", "profile-manifest"],
                0,
                "",
                "",
            )

        with mock.patch.object(
            kernel_compile,
            "run_seed_tool",
            side_effect=mutate_live_seed,
        ):
            with self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                r"(?i)(seed inputs changed|SHA-256 differs|seed)",
            ):
                kernel_compile.write_profile_input_manifest(
                    root,
                    output,
                    manifest=manifest,
                )

        self.assertEqual(output.read_bytes(), b"old manifest\n")

    def test_output_appearance_and_disappearance_block_publication(self):
        for drift in ("appeared", "disappeared"):
            with self.subTest(drift=drift):
                root, _source, _header, _seed, _manifest, _output = (
                    self._profile_fixture()
                )
                output = root / "build" / "doom-inputs.json"
                output.parent.mkdir()
                if drift == "disappeared":
                    output.write_bytes(b"old manifest\n")

                def change_output(
                    manifest_path,
                    working_directory,
                    tool_name,
                    arguments,
                    **_options,
                ):
                    snapshot, candidate = self._checked_manifest_paths(
                        manifest_path,
                        working_directory,
                        tool_name,
                        arguments,
                    )
                    _document, _captured, oracle = _profile_snapshot_oracle(
                        snapshot.read_bytes()
                    )
                    candidate.write_bytes(oracle)
                    if drift == "appeared":
                        output.write_bytes(b"competing publisher")
                    else:
                        output.unlink()
                    return subprocess.CompletedProcess(
                        ["cupidobj", "profile-manifest"],
                        0,
                        "",
                        "",
                    )

                with mock.patch.object(
                    kernel_compile,
                    "run_seed_tool",
                    side_effect=change_output,
                ):
                    with self.assertRaisesRegex(
                        kernel_compile.KernelCompileError,
                        rf"(?i)(output {drift}|{drift})",
                    ):
                        self._write_manifest_with_checked_seed(root, output)

                if drift == "appeared":
                    self.assertEqual(output.read_bytes(), b"competing publisher")
                else:
                    self.assertFalse(output.exists())

    @unittest.skipIf(os.name == "nt", "open Windows lock prevents directory rename")
    def test_full_writer_rejects_output_directory_replacement(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        output = root / "build" / "doom-inputs.json"
        output.parent.mkdir()
        output.write_bytes(b"old manifest\n")

        def replace_directory(
            manifest_path,
            working_directory,
            tool_name,
            arguments,
            **_options,
        ):
            snapshot, candidate = self._checked_manifest_paths(
                manifest_path,
                working_directory,
                tool_name,
                arguments,
            )
            _document, _captured, oracle = _profile_snapshot_oracle(
                snapshot.read_bytes()
            )
            candidate.write_bytes(oracle)
            output.parent.rename(root / "old-build")
            output.parent.mkdir()
            output.write_bytes(b"competing directory")
            return subprocess.CompletedProcess(
                ["cupidobj", "profile-manifest"],
                0,
                "",
                "",
            )

        with mock.patch.object(
            kernel_compile,
            "run_seed_tool",
            side_effect=replace_directory,
        ):
            with self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                r"(?i)(directory changed|changed while)",
            ):
                self._write_manifest_with_checked_seed(root, output)

        self.assertEqual(output.read_bytes(), b"competing directory")

    def test_cross_process_lock_rejects_a_second_publisher(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        output = root / "build" / "doom-inputs.json"
        output.parent.mkdir()
        lock = kernel_compile._acquire_profile_manifest_lock(output)
        try:
            result = subprocess.run(
                [
                    sys.executable,
                    str(REPO_ROOT / "tools" / "cupidc_kernel_compile.py"),
                    "--root",
                    str(root),
                    "--write-profile-input-manifest",
                    str(output),
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
                timeout=30,
            )
        finally:
            kernel_compile._release_profile_manifest_lock(lock)

        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertRegex(result.stderr, r"(?i)(lock|publisher|active)")
        self.assertFalse(output.exists())

    def test_post_publication_failure_reports_after_the_atomic_commit(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        output = root / "build" / "doom-inputs.json"
        output.parent.mkdir()
        output.write_bytes(b"old manifest\n")
        published = []

        def write_candidate(
            manifest_path,
            working_directory,
            tool_name,
            arguments,
            **_options,
        ):
            snapshot, candidate = self._checked_manifest_paths(
                manifest_path,
                working_directory,
                tool_name,
                arguments,
            )
            _document, _captured, oracle = _profile_snapshot_oracle(
                snapshot.read_bytes()
            )
            candidate.write_bytes(oracle)
            published.append(oracle)
            return subprocess.CompletedProcess(
                ["cupidobj", "profile-manifest"],
                0,
                "",
                "",
            )

        capture_file = kernel_compile._capture_profile_file

        def fail_post_commit(path, label, **options):
            if label == "published profile input manifest":
                raise kernel_compile.KernelCompileError(
                    "deliberate post-publication verification failure"
                )
            return capture_file(path, label, **options)

        with (
            mock.patch.object(
                kernel_compile,
                "run_seed_tool",
                side_effect=write_candidate,
            ),
            mock.patch.object(
                kernel_compile,
                "_capture_profile_file",
                side_effect=fail_post_commit,
            ),
        ):
            with self.assertRaisesRegex(
                kernel_compile.KernelCompileError,
                "post-publication verification failure",
            ):
                self._write_manifest_with_checked_seed(root, output)

        self.assertEqual(len(published), 1)
        self.assertEqual(output.read_bytes(), published[0])

    def test_live_profile_drift_after_cupidobj_blocks_publication(self):
        for drift in ("header bytes", "header membership"):
            with self.subTest(drift=drift):
                root, _source, header, _seed, _manifest, _output = (
                    self._profile_fixture()
                )
                output = root / "build" / "doom-inputs.json"
                output.parent.mkdir()
                old_payload = f"old manifest before {drift}\n".encode("ascii")
                output.write_bytes(old_payload)
                candidates = []
                snapshots = []

                def change_live_input(
                    manifest_path,
                    working_directory,
                    tool_name,
                    arguments,
                    **_options,
                ):
                    snapshot, candidate = self._checked_manifest_paths(
                        manifest_path,
                        working_directory,
                        tool_name,
                        arguments,
                    )
                    frozen = snapshot.read_bytes()
                    _document, captured, oracle = _profile_snapshot_oracle(
                        frozen
                    )
                    snapshots.append(captured)
                    if drift == "header bytes":
                        header.write_text(
                            "#define DOOM_SHADOW 99\n",
                            encoding="utf-8",
                        )
                    else:
                        (root / "kernel" / "doom" / "late-profile.h").write_text(
                            "#define LATE_PROFILE 1\n",
                            encoding="utf-8",
                        )
                    candidate.write_bytes(oracle)
                    candidates.append(candidate)
                    return subprocess.CompletedProcess(
                        ["cupidobj", "profile-manifest"],
                        0,
                        "",
                        "",
                    )

                original_header = header.read_bytes()
                with mock.patch.object(
                    kernel_compile,
                    "run_seed_tool",
                    side_effect=change_live_input,
                    create=True,
                ):
                    with self.assertRaisesRegex(
                        kernel_compile.KernelCompileError,
                        r"(?i)(changed|drift)",
                    ):
                        self._write_manifest_with_checked_seed(root, output)

                self.assertEqual(output.read_bytes(), old_payload)
                self.assertEqual(len(snapshots), 1)
                relative_header = header.relative_to(root).as_posix()
                self.assertEqual(
                    snapshots[0][relative_header],
                    original_header,
                )
                self.assertTrue(
                    all(not candidate.exists() for candidate in candidates)
                )

    def test_only_one_profile_manifest_publisher_can_hold_an_output(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        output = root / "build" / "doom-inputs.json"
        output.parent.mkdir()
        started = threading.Event()
        release = threading.Event()
        call_guard = threading.Lock()
        call_count = 0

        def blocking_runner(
            manifest_path,
            working_directory,
            tool_name,
            arguments,
            **_options,
        ):
            nonlocal call_count
            snapshot, candidate = self._checked_manifest_paths(
                manifest_path,
                working_directory,
                tool_name,
                arguments,
            )
            with call_guard:
                call_count += 1
                call_number = call_count
            if call_number == 1:
                started.set()
                if not release.wait(10):
                    raise AssertionError(
                        "timed out while holding the profile manifest lock"
                    )
            _document, _captured, oracle = _profile_snapshot_oracle(
                snapshot.read_bytes()
            )
            candidate.write_bytes(oracle)
            return subprocess.CompletedProcess(
                ["cupidobj", "profile-manifest"],
                0,
                "",
                "",
            )

        with mock.patch.object(
            kernel_compile,
            "run_seed_tool",
            side_effect=blocking_runner,
            create=True,
        ):
            with ThreadPoolExecutor(max_workers=2) as pool:
                first = pool.submit(
                    self._write_manifest_with_checked_seed,
                    root,
                    output,
                )
                try:
                    self.assertTrue(
                        started.wait(5),
                        "the checked publisher never reached CupidObj",
                    )
                    second = pool.submit(
                        self._write_manifest_with_checked_seed,
                        root,
                        output,
                    )
                    with self.assertRaisesRegex(
                        kernel_compile.KernelCompileError,
                        r"(?i)(another|already|lock|publisher|progress)",
                    ):
                        second.result(timeout=5)
                finally:
                    release.set()
                first.result(timeout=10)

        self.assertEqual(call_count, 1)
        self.assertTrue(output.is_file())

    def test_unchanged_checked_manifest_keeps_its_timestamp(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        output = root / "build" / "doom-inputs.json"
        output.parent.mkdir()
        calls = []

        def run_checked(
            manifest_path,
            working_directory,
            tool_name,
            arguments,
            **_options,
        ):
            snapshot, candidate = self._checked_manifest_paths(
                manifest_path,
                working_directory,
                tool_name,
                arguments,
            )
            frozen = snapshot.read_bytes()
            _document, _captured, oracle = _profile_snapshot_oracle(frozen)
            candidate.write_bytes(oracle)
            calls.append(frozen)
            return subprocess.CompletedProcess(
                ["cupidobj", "profile-manifest"],
                0,
                "",
                "",
            )

        with mock.patch.object(
            kernel_compile,
            "run_seed_tool",
            side_effect=run_checked,
            create=True,
        ):
            self.assertTrue(
                self._write_manifest_with_checked_seed(root, output)
            )
            stable_time = 1_700_000_000_000_000_000
            os.utime(output, ns=(stable_time, stable_time))
            self.assertFalse(
                self._write_manifest_with_checked_seed(root, output)
            )

        self.assertEqual(len(calls), 2)
        self.assertEqual(calls[0], calls[1])
        self.assertEqual(output.stat().st_mtime_ns, stable_time)

    def test_profile_manifest_cli_passes_the_requested_seed_manifest(self):
        root, _source, _header, _seed, _manifest, _output = (
            self._profile_fixture()
        )
        output = root / "build" / "doom-inputs.json"
        with mock.patch.object(
            kernel_compile,
            "write_profile_input_manifest",
            return_value=False,
        ) as write_manifest:
            status = kernel_compile.main(
                [
                    "--root",
                    str(root),
                    "--manifest",
                    str(SEED_MANIFEST),
                    "--write-profile-input-manifest",
                    str(output),
                ]
            )

        self.assertEqual(status, 0)
        write_manifest.assert_called_once_with(
            root,
            output,
            manifest=SEED_MANIFEST,
        )

    def test_make_manifest_rule_depends_on_the_checked_seed_trust_unit(self):
        makefile = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        start = makefile.index("$(DOOM_CUPIDC_INPUT_MANIFEST):")
        end = makefile.index("\n\n", start)
        rule = makefile[start:end]
        lines = rule.splitlines()
        recipe_start = next(
            index
            for index, line in enumerate(lines)
            if line.startswith("ifneq")
        )
        prerequisites = "\n".join(lines[:recipe_start])
        recipe = "\n".join(lines[recipe_start:])
        with self.subTest(contract="checked seed prerequisites"):
            self.assertIn("Makefile $(PRODUCTION_SEED_INPUTS)", prerequisites)
            self.assertNotIn("tools/", prerequisites)
        with self.subTest(contract="selected seed manifest"):
            self.assertIn(
                "--seed-manifest $(PRODUCTION_SEED_MANIFEST)",
                recipe,
            )
            self.assertIn("generate-profile-manifest", recipe)
            self.assertNotIn("$(PYTHON)", recipe)

    def test_parallel_make_profiles_before_source_directory_writers(self):
        makefile = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        barrier = re.search(
            r"(?m)^\$\(filter kernel/% drivers/% toolchain/%,\$\(BOOTSTRAP_ARTIFACTS\)\).*?"
            r"\| \$\(DOOM_CUPIDC_INPUT_MANIFEST\)$",
            makefile.replace("\\\n", ""),
        )
        self.assertIsNotNone(barrier, "source-directory writers need the profile barrier")
        generated = [
            f"kernel/util/{name}_programs_gen.cc"
            for name in ("bin", "docs", "demos")
        ]
        artifacts = [
            "drivers/serial.o", "kernel/core/kernel.o", "toolchain/ctool.o",
            "kernel/smp_trampoline.bin", "kernel/cpu/ksyms_data.cc",
            "kernel/kernel.elf.pass1", "kernel/kernel.elf", "kernel/kernel.bin",
            *(str(Path(path).with_suffix(".o")).replace("\\", "/") for path in generated),
        ]
        with tempfile.TemporaryDirectory(prefix="cupid-profile-order-") as td:
            root = Path(td)
            helper = root / "writer.py"
            helper.write_text(
                "import sys, time\n"
                "from pathlib import Path\n"
                "output = Path(sys.argv[1])\n"
                "if str(output) == 'profile.done':\n"
                "    deadline = time.monotonic() + 10\n"
                "    while not Path('outside.bin').exists():\n"
                "        if time.monotonic() >= deadline:\n"
                "            raise SystemExit('unrelated work was serialized')\n"
                "        time.sleep(0.01)\n"
                "elif str(output) != 'outside.bin' and not Path('profile.done').exists():\n"
                "    raise SystemExit('source-directory writer ran before profile')\n"
                "output.parent.mkdir(parents=True, exist_ok=True)\n"
                "output.write_bytes(b'complete')\n",
                encoding="utf-8",
            )
            command = f'"{Path(sys.executable).as_posix()}" writer.py "$@"'
            rules = (
                ".SUFFIXES:\n.PHONY: all FORCE\n"
                f"BOOTSTRAP_ARTIFACTS := {' '.join(artifacts)}\n"
                "DOOM_CUPIDC_INPUT_MANIFEST := profile.done\n"
                "all: $(BOOTSTRAP_ARTIFACTS) outside.bin\n"
                f"{barrier.group(0)}\n"
                f"profile.done: FORCE\n\t{command}\n"
                f"outside.bin: \n\t{command}\n"
                f"{' '.join([*artifacts, *generated])}:\n\t{command}\n"
                + "".join(
                    f"{Path(path).with_suffix('.o').as_posix()}: {path}\n"
                    for path in generated
                )
            )
            (root / "Makefile").write_text(
                rules.replace(barrier.group(0), ""), encoding="utf-8"
            )
            unordered = subprocess.run(
                ("make", "-j8", "all"), cwd=root, text=True,
                capture_output=True, timeout=30, check=False,
            )
            self.assertNotEqual(unordered.returncode, 0)
            self.assertIn(
                "source-directory writer ran before profile",
                unordered.stdout + unordered.stderr,
            )
            for path in ["profile.done", "outside.bin", *artifacts, *generated]:
                (root / path).unlink(missing_ok=True)
            (root / "Makefile").write_text(rules, encoding="utf-8")
            fixed_time = 1_700_000_000_000_000_000
            for attempt in range(2):
                result = subprocess.run(
                    ("make", "-j8", "all"), cwd=root, text=True,
                    capture_output=True, timeout=30, check=False,
                )
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                for path in [*artifacts, *generated]:
                    output = root / path
                    self.assertEqual(output.read_bytes(), b"complete")
                    if attempt:
                        self.assertEqual(output.stat().st_mtime_ns, fixed_time)
                    else:
                        os.utime(output, ns=(fixed_time, fixed_time))

    def test_make_profile_manifest_uses_typed_publisher_on_both_hosts(self):
        target = "build/bootstrap/doom-cupidc-inputs.json"
        marker = "PROFILE_HOST_TOOL_MUST_NOT_RUN"
        for platform, suffix in (("Windows_NT", "exe"), ("Linux", "elf")):
            with self.subTest(platform=platform):
                result = subprocess.run(
                    (
                        "make", "--dry-run", "--always-make", target,
                        f"OS={platform}",
                        f"PYTHON={marker}",
                        f"CUPIDOBJ={marker}",
                        f"CHECKED_SEED_RUN={marker}",
                        f"PRODUCTION_SEED_DIRECTORY={marker}",
                        f"PRODUCTION_SEED_SUFFIX={marker}",
                        f"PRODUCTION_SEED_INPUTS={marker}",
                        f"DOOM_CUPIDC_INPUT_MANIFEST={marker}",
                    ),
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=60,
                    check=False,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn(marker, result.stdout)
                self.assertIn(
                    f"cupidbuild.{suffix} generate-profile-manifest",
                    result.stdout,
                )
                self.assertIn(f"--output {target}", result.stdout)
                self.assertEqual(
                    "test ! -L build && test ! -L build/bootstrap && mkdir -p build/bootstrap"
                    in result.stdout,
                    platform != "Windows_NT",
                )

    @unittest.skipUnless(os.name == "posix", "requires POSIX Make")
    def test_make_profile_parent_preparation_rejects_existing_symlinks(self):
        for relative in ("build", "build/bootstrap"):
            with self.subTest(parent=relative):
                root, *_ = self._profile_fixture()
                shutil.copyfile(REPO_ROOT / "Makefile", root / "Makefile")
                seed = Path("bootstrap/seeds/i386-linux")
                shutil.copytree(REPO_ROOT / seed, root / seed)
                external = tempfile.TemporaryDirectory(prefix="cupid-profile-outside-")
                self.addCleanup(external.cleanup)
                outside = Path(external.name)
                sentinel = outside / "sentinel"
                sentinel.write_bytes(b"unchanged")
                original_time = outside.stat().st_mtime_ns
                link = root / relative
                link.parent.mkdir(parents=True, exist_ok=True)
                link.symlink_to(outside, target_is_directory=True)

                result = subprocess.run(
                    ("make", "build/bootstrap/doom-cupidc-inputs.json", "OS=Linux"),
                    cwd=root,
                    text=True,
                    capture_output=True,
                    timeout=60,
                    check=False,
                )

                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(list(outside.iterdir()), [sentinel])
                self.assertEqual(sentinel.read_bytes(), b"unchanged")
                self.assertEqual(outside.stat().st_mtime_ns, original_time)
                self.assertNotIn("generate-profile-manifest", result.stdout)
                self.assertTrue(link.is_symlink())

if __name__ == "__main__":
    unittest.main()

## @file
#  @brief Check Doom profile publication and recovery.
import errno
import json
import os
import stat
import tempfile
import threading
import time
import unittest
from tests.c_source_spelling import _c_spelling_ranges
from pathlib import Path
from tools.cupidc_kernel_compile import _profile_input_manifest

from tests.cupidbuild_cli_fixtures import REPO_ROOT, TOOLCHAIN_ROOT


class ProfilePublicationCases:
    def test_typed_profile_manifest_transaction_matches_the_python_oracle(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-success-", dir=REPO_ROOT
        ) as temporary:
            output = Path(temporary) / "doom-cupidc-inputs.json"
            expected = (
                json.dumps(
                    _profile_input_manifest(REPO_ROOT),
                    indent=2,
                    sort_keys=True,
                )
                + "\n"
            ).encode("utf-8")
            before = self._private_roots()

            result = self._run_profile_manifest(output)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual((result.stdout, result.stderr), ("", ""))
            self.assertEqual(output.read_bytes(), expected)
            self.assertEqual(self._private_roots(), before)

    def test_typed_profile_manifest_preserves_an_unchanged_timestamp(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-unchanged-", dir=REPO_ROOT
        ) as temporary:
            output = Path(temporary) / "doom-cupidc-inputs.json"
            first = self._run_profile_manifest(output)
            self.assertEqual(first.returncode, 0, first.stderr)
            old_time = 1_600_000_000_000_000_000
            os.utime(output, ns=(old_time, old_time))

            second = self._run_profile_manifest(output)

            self.assertEqual(second.returncode, 0, second.stderr)
            self.assertEqual(output.stat().st_mtime_ns, old_time)

    def test_typed_profile_manifest_replaces_a_previous_output(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-replace-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            output = root / "doom-cupidc-inputs.json"
            output.write_bytes(b"previous profile manifest")
            expected = (
                json.dumps(
                    _profile_input_manifest(root),
                    indent=2,
                    sort_keys=True,
                )
                + "\n"
            ).encode("utf-8")

            result = self._run_profile_manifest(output, manifest=manifest, root=root)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(output.read_bytes(), expected)
            self.assertEqual(self._private_roots(root), set())
            self.assertEqual(list(root.glob(".cupidbuild-old-*")), [])

    def test_typed_profile_manifest_handles_the_fixed_parent_on_a_clean_root(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-clean-parent-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            output = root / "build" / "bootstrap" / "doom-cupidc-inputs.json"
            self.assertFalse((root / "build").exists())

            result = self._run_profile_manifest(output, manifest=manifest, root=root)

            if os.name == "nt":
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual((result.stdout, result.stderr), ("", ""))
                self.assertTrue((root / "build").is_dir())
                self.assertTrue((root / "build" / "bootstrap").is_dir())
                self.assertEqual(
                    json.loads(output.read_text(encoding="utf-8"))["schema"],
                    "cupid.doom-profile-inputs.v1",
                )
            else:
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("must already exist on POSIX", result.stderr)
                self.assertFalse((root / "build").exists())
                self.assertFalse(output.exists())
            self.assertEqual(self._private_roots(root), set())

    def test_typed_profile_manifest_rejects_a_fixed_parent_collision(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-parent-collision-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            build = root / "build"
            build.write_bytes(b"owned collision")
            output = build / "bootstrap" / "doom-cupidc-inputs.json"

            result = self._run_profile_manifest(output, manifest=manifest, root=root)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("profile parent", result.stderr)
            self.assertEqual(build.read_bytes(), b"owned collision")
            self.assertEqual(self._private_roots(root), set())

    def test_typed_profile_manifest_preserves_a_bootstrap_parent_collision(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-bootstrap-collision-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            build = root / "build"
            build.mkdir()
            bootstrap = build / "bootstrap"
            bootstrap.write_bytes(b"owned collision")
            output = bootstrap / "doom-cupidc-inputs.json"

            result = self._run_profile_manifest(output, manifest=manifest, root=root)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("profile parent bootstrap component", result.stderr)
            self.assertEqual(bootstrap.read_bytes(), b"owned collision")
            self.assertTrue(build.is_dir())
            self.assertEqual(self._private_roots(root), set())

    def test_typed_profile_manifest_rejects_a_linked_fixed_parent(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-parent-link-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            target = root / "outside"
            target.mkdir()
            build = root / "build"
            try:
                build.symlink_to(target, target_is_directory=True)
            except OSError as error:
                self.skipTest(f"directory symlink creation is unavailable: {error}")
            output = build / "bootstrap" / "doom-cupidc-inputs.json"

            result = self._run_profile_manifest(output, manifest=manifest, root=root)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("profile parent", result.stderr)
            self.assertTrue(build.is_symlink())
            self.assertEqual(list(target.iterdir()), [])
            self.assertEqual(self._private_roots(root), set())

    def test_typed_profile_manifest_rolls_back_its_fixed_parent_after_failure(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-parent-rollback-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            unlisted = root / "kernel" / "doom" / "unlisted.cc"
            unlisted.write_text("int unlisted;\n", encoding="ascii")
            output = root / "build" / "bootstrap" / "doom-cupidc-inputs.json"
            if os.name != "nt":
                output.parent.mkdir(parents=True)

            result = self._run_profile_manifest(output, manifest=manifest, root=root)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("approved source cohort", result.stderr)
            self.assertFalse(output.exists())
            if os.name == "nt":
                self.assertFalse((root / "build" / "bootstrap").exists())
                self.assertFalse((root / "build").exists())
            else:
                self.assertTrue((root / "build" / "bootstrap").is_dir())
                self.assertTrue((root / "build").is_dir())
                self.assertEqual(list((root / "build" / "bootstrap").iterdir()), [])
                self.assertEqual(
                    [entry.name for entry in (root / "build").iterdir()],
                    ["bootstrap"],
                )
            self.assertEqual(self._private_roots(root), set())

    def test_typed_profile_manifest_does_not_remove_foreign_rollback_contents(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-parent-foreign-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            document = json.loads(manifest.read_text(encoding="utf-8"))
            artifact = next(
                item for item in document["artifacts"] if item["name"] == "cupidobj"
            )
            cupidobj = manifest.parent / artifact["file"]
            payload = cupidobj.read_bytes()
            profile_key = b'"profiles":'
            self.assertEqual(payload.count(profile_key), 1)
            self._replace_seed_tool_bytes(
                manifest,
                "cupidobj",
                payload.replace(profile_key, b'"profilet":'),
            )
            output = root / "build" / "bootstrap" / "doom-cupidc-inputs.json"
            if os.name != "nt":
                output.parent.mkdir(parents=True)
            foreign = output.parent / "foreign.txt"
            lock = Path(str(output) + ".cupidbuild.lock")
            changed = threading.Event()

            def add_foreign_content_after_parent_is_in_use():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    if lock.is_file():
                        foreign.write_bytes(b"foreign content")
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=add_foreign_content_after_parent_is_in_use, daemon=True
            )
            mutator.start()
            result = self._run_profile_manifest(output, manifest=manifest, root=root)
            mutator.join(timeout=20)

            self.assertTrue(changed.is_set(), "the prepared parent was not observed")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("differs from the independent renderer", result.stderr)
            if os.name == "nt":
                self.assertIn("profile parent cleanup failed", result.stderr)
            else:
                self.assertNotIn("profile parent cleanup failed", result.stderr)
            self.assertEqual(foreign.read_bytes(), b"foreign content")
            self.assertTrue(output.parent.is_dir())
            self.assertTrue((root / "build").is_dir())
            self.assertEqual(self._private_roots(root), set())

    @unittest.skipIf(os.name == "nt", "Windows creates and pins the parent")
    def test_typed_profile_manifest_requires_a_preexisting_bootstrap_on_posix(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-missing-bootstrap-"
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            build = root / "build"
            build.mkdir()
            owner = build / "owner.txt"
            owner.write_bytes(b"preexisting build directory")
            output = root / "build" / "bootstrap" / "doom-cupidc-inputs.json"

            result = self._run_profile_manifest(
                output,
                manifest=manifest,
                root=root,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("bootstrap component must already exist", result.stderr)
            self.assertNotIn("profile parent cleanup failed", result.stderr)
            self.assertFalse(output.exists())
            self.assertEqual(owner.read_bytes(), b"preexisting build directory")
            self.assertEqual([entry.name for entry in build.iterdir()], ["owner.txt"])
            self.assertEqual(self._private_roots(root), set())

    def test_typed_profile_manifest_rejects_a_preexisting_build_replacement(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-existing-build-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            build = root / "build"
            build.mkdir()
            owner = build / "owner.txt"
            owner.write_bytes(b"original build directory")
            output = build / "bootstrap" / "doom-cupidc-inputs.json"
            environment = os.environ.copy()
            environment[
                "CUPIDBUILD_PROFILE_PARENT_TEST_REPLACE_EXISTING"
            ] = "build"

            result = self._run_profile_manifest(
                output,
                manifest=manifest,
                root=root,
                cli=self.race_cli_path,
                env=environment,
            )

            displaced = root / "displaced-existing-build"
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("build component changed", result.stderr)
            self.assertEqual(
                (displaced / owner.name).read_bytes(),
                b"original build directory",
            )
            self.assertEqual(list(build.iterdir()), [])
            self.assertFalse(output.exists())
            self.assertEqual(self._private_roots(root), set())

    def test_typed_profile_manifest_rejects_a_preexisting_bootstrap_replacement(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-existing-bootstrap-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            build = root / "build"
            build.mkdir()
            bootstrap = build / "bootstrap"
            bootstrap.mkdir()
            owner = bootstrap / "owner.txt"
            owner.write_bytes(b"original bootstrap directory")
            output = bootstrap / "doom-cupidc-inputs.json"
            environment = os.environ.copy()
            environment[
                "CUPIDBUILD_PROFILE_PARENT_TEST_REPLACE_EXISTING"
            ] = "bootstrap"

            result = self._run_profile_manifest(
                output,
                manifest=manifest,
                root=root,
                cli=self.race_cli_path,
                env=environment,
            )

            displaced = build / "displaced-existing-bootstrap"
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("bootstrap component changed", result.stderr)
            self.assertEqual(
                (displaced / owner.name).read_bytes(),
                b"original bootstrap directory",
            )
            self.assertEqual(list(bootstrap.iterdir()), [])
            self.assertFalse(output.exists())
            self.assertEqual(self._private_roots(root), set())

    @unittest.skipIf(os.name == "nt", "the adversarial hook covers POSIX openat")
    def test_typed_profile_manifest_rejects_a_root_replaced_before_open(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-root-pre-open-replacement-"
        ) as temporary:
            container = Path(temporary)
            root = container / "repository-root"
            root.mkdir()
            manifest = self._copy_profile_repository(root)
            output = root / "build" / "bootstrap" / "doom-cupidc-inputs.json"
            displaced = container / "displaced-root-component"
            environment = os.environ.copy()
            environment[
                "CUPIDBUILD_PROFILE_PARENT_TEST_REPLACE_ROOT_BEFORE_OPEN"
            ] = root.name

            result = self._run_profile_manifest(
                output,
                manifest=manifest,
                root=root,
                cli=self.race_cli_path,
                env=environment,
            )

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("repository root cannot be pinned", result.stderr)
            self.assertTrue(root.is_symlink())
            self.assertTrue(displaced.is_dir())
            self.assertFalse((displaced / "build").exists())
            self.assertTrue(manifest.is_file())
            self.assertEqual(self._private_roots(displaced), set())

    def test_typed_profile_manifest_live_lock_preserves_previous_output(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-lock-", dir=REPO_ROOT
        ) as temporary:
            output = Path(temporary) / "doom-cupidc-inputs.json"
            lock = Path(str(output) + ".cupidbuild.lock")
            output.write_bytes(b"last known good profile manifest")
            lock.write_bytes(f"{os.getpid()}\n".encode("ascii"))

            result = self._run_profile_manifest(output)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("live process", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good profile manifest")
            self.assertTrue(lock.is_file())

    def test_typed_profile_manifest_rejects_a_lock_path_that_would_truncate(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-long-lock-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            lock_suffix = ".cupidbuild.lock"
            output_path_length = 8192 - len(lock_suffix)
            output_name_length = output_path_length - len(str(root)) - 1
            self.assertGreater(output_name_length, 255)
            output = root / ("x" * (output_name_length - 5) + ".json")
            self.assertEqual(len(str(output)), output_path_length)
            before_entries = {entry.name for entry in root.iterdir()}
            before_private = self._private_roots(root)

            result = self._run_profile_manifest(
                output, manifest=manifest, root=root
            )

            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertEqual(
                result.stderr,
                "cupidbuild: guarded artifact paths are invalid\n",
            )
            self.assertEqual(
                {entry.name for entry in root.iterdir()}, before_entries
            )
            self.assertEqual(self._private_roots(root), before_private)

    def test_typed_profile_manifest_rejects_an_unlisted_doom_source(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-membership-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            unlisted = root / "kernel" / "doom" / "unlisted.cc"
            unlisted.write_text("int unlisted;\n", encoding="ascii")
            output = root / "doom-cupidc-inputs.json"
            output.write_bytes(b"last known good profile manifest")

            result = self._run_profile_manifest(output, manifest=manifest, root=root)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("approved source cohort", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good profile manifest")
            self.assertEqual(self._private_roots(root), set())

    def test_typed_profile_manifest_rejects_a_link_in_the_header_closure(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-link-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            linked = root / "drivers" / "linked.h"
            try:
                linked.symlink_to(root / "drivers" / "ata.h")
            except OSError as error:
                self.skipTest(f"symlink creation is unavailable: {error}")
            output = root / "doom-cupidc-inputs.json"
            output.write_bytes(b"last known good profile manifest")

            result = self._run_profile_manifest(output, manifest=manifest, root=root)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("profile closure is malformed", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good profile manifest")
            self.assertEqual(self._private_roots(root), set())

    def test_typed_profile_manifest_rejects_an_overfull_directory_walk(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-directory-limit-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            drivers = root / "drivers"
            existing = 1 + sum(1 for path in drivers.rglob("*") if path.is_dir())
            self.assertEqual(existing, 1)
            self._add_directories(drivers, 512)
            output = root / "doom-cupidc-inputs.json"
            output.write_bytes(b"last known good profile manifest")

            result = self._run_profile_manifest(output, manifest=manifest, root=root)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("profile closure is malformed", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good profile manifest")
            self.assertEqual(self._private_roots(root), set())

    def test_typed_profile_manifest_accepts_exactly_512_directories(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-directory-boundary-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            drivers = root / "drivers"
            existing = 1 + sum(1 for path in drivers.rglob("*") if path.is_dir())
            self.assertEqual(existing, 1)
            self._add_directories(drivers, 511)
            output = root / "doom-cupidc-inputs.json"

            result = self._run_profile_manifest(output, manifest=manifest, root=root)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(output.is_file())
            self.assertEqual(self._private_roots(root), set())

    def test_typed_profile_manifest_rejects_a_restored_directory_at_publication(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-directory-race-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            output = root / "doom-cupidc-inputs.json"
            output.write_bytes(b"last known good profile manifest")
            ready = root / "directory-boundary-ready"
            resume = root / "directory-boundary-resume"
            drivers = root / "drivers"
            original_times = drivers.stat()
            os.utime(
                drivers,
                ns=(
                    original_times.st_atime_ns,
                    original_times.st_mtime_ns
                    - original_times.st_mtime_ns % 1_000_000_000,
                ),
            )
            original_times = drivers.stat()
            transient = drivers / "late-empty-directory"
            changed = threading.Event()
            mutation_errors = []

            def change_directory_before_the_final_validation():
                try:
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline:
                        if ready.is_file():
                            transient.mkdir()
                            transient.rmdir()
                            os.utime(
                                drivers,
                                ns=(
                                    original_times.st_atime_ns,
                                    original_times.st_mtime_ns,
                                ),
                            )
                            changed.set()
                            return
                        time.sleep(0.001)
                except Exception as error:
                    mutation_errors.append(error)
                finally:
                    try:
                        resume.write_bytes(b"continue")
                    except Exception as error:
                        mutation_errors.append(error)

            environment = os.environ.copy()
            environment["CUPIDBUILD_PROFILE_TEST_DIRECTORY_READY"] = str(ready)
            environment["CUPIDBUILD_PROFILE_TEST_DIRECTORY_RESUME"] = str(resume)
            mutator = threading.Thread(
                target=change_directory_before_the_final_validation, daemon=True
            )
            mutator.start()
            result = self._run_profile_manifest(
                output,
                manifest=manifest,
                root=root,
                cli=self.race_cli_path,
                env=environment,
            )
            mutator.join(timeout=35)

            self.assertFalse(mutator.is_alive(), "the directory mutator did not stop")
            self.assertFalse(mutation_errors, repr(mutation_errors))
            self.assertTrue(
                changed.is_set(), "the final directory validation was not observed"
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("discovered directory closure changed", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good profile manifest")
            self.assertFalse(transient.exists())
            self.assertEqual(drivers.stat().st_mtime_ns, original_times.st_mtime_ns)
            self.assertEqual(self._private_roots(root), set())

    def test_typed_profile_manifest_rejects_a_restored_directory_after_first_pass(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-directory-second-pass-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            output = root / "doom-cupidc-inputs.json"
            output.write_bytes(b"last known good profile manifest")
            ready = root / "directory-first-pass-ready"
            resume = root / "directory-first-pass-resume"
            drivers = root / "drivers"
            original_times = drivers.stat()
            os.utime(
                drivers,
                ns=(
                    original_times.st_atime_ns,
                    original_times.st_mtime_ns
                    - original_times.st_mtime_ns % 1_000_000_000,
                ),
            )
            original_times = drivers.stat()
            transient = drivers / "late-empty-directory"
            changed = threading.Event()
            mutation_errors = []

            def change_directory_after_first_validation_pass():
                try:
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline:
                        if ready.is_file():
                            transient.mkdir()
                            transient.rmdir()
                            os.utime(
                                drivers,
                                ns=(
                                    original_times.st_atime_ns,
                                    original_times.st_mtime_ns,
                                ),
                            )
                            changed.set()
                            return
                        time.sleep(0.001)
                except Exception as error:
                    mutation_errors.append(error)
                finally:
                    try:
                        resume.write_bytes(b"continue")
                    except Exception as error:
                        mutation_errors.append(error)

            environment = os.environ.copy()
            environment[
                "CUPIDBUILD_PROFILE_TEST_DIRECTORY_AFTER_FIRST_PASS_READY"
            ] = str(ready)
            environment[
                "CUPIDBUILD_PROFILE_TEST_DIRECTORY_AFTER_FIRST_PASS_RESUME"
            ] = str(resume)
            mutator = threading.Thread(
                target=change_directory_after_first_validation_pass,
                daemon=True,
            )
            mutator.start()
            result = self._run_profile_manifest(
                output,
                manifest=manifest,
                root=root,
                cli=self.race_cli_path,
                env=environment,
            )
            mutator.join(timeout=35)

            self.assertFalse(mutator.is_alive(), "the directory mutator did not stop")
            self.assertFalse(mutation_errors, repr(mutation_errors))
            self.assertTrue(
                changed.is_set(), "the first directory validation pass was not observed"
            )
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("discovered directory closure changed", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good profile manifest")
            self.assertFalse(transient.exists())
            self.assertEqual(drivers.stat().st_mtime_ns, original_times.st_mtime_ns)
            self.assertEqual(self._private_roots(root), set())

    def test_typed_profile_manifest_cleans_up_after_source_drift_after_install(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-post-install-source-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            output = root / "doom-cupidc-inputs.json"
            previous = b"last known good profile manifest"
            output.write_bytes(previous)
            ready = root / "publication-ready"
            resume = root / "publication-resume"
            drivers = root / "drivers"
            original_times = drivers.stat()
            os.utime(
                drivers,
                ns=(
                    original_times.st_atime_ns,
                    original_times.st_mtime_ns
                    - original_times.st_mtime_ns % 1_000_000_000,
                ),
            )
            original_times = drivers.stat()
            transient = drivers / "late-empty-directory"
            changed = threading.Event()
            mutation_errors = []

            def change_source_closure_after_candidate_install():
                try:
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline:
                        if ready.is_file():
                            transient.mkdir()
                            transient.rmdir()
                            os.utime(
                                drivers,
                                ns=(
                                    original_times.st_atime_ns,
                                    original_times.st_mtime_ns,
                                ),
                            )
                            changed.set()
                            return
                        time.sleep(0.001)
                    mutation_errors.append("publication checkpoint was not observed")
                except Exception as error:  # pragma: no cover - surfaced below
                    mutation_errors.append(repr(error))
                finally:
                    try:
                        resume.write_bytes(b"continue")
                    except Exception as error:  # pragma: no cover - surfaced below
                        mutation_errors.append(repr(error))

            environment = os.environ.copy()
            environment["CUPIDBUILD_PUBLICATION_TEST_PHASE"] = "after-install"
            environment["CUPIDBUILD_PUBLICATION_TEST_READY"] = str(ready)
            environment["CUPIDBUILD_PUBLICATION_TEST_RESUME"] = str(resume)
            mutator = threading.Thread(
                target=change_source_closure_after_candidate_install,
                daemon=True,
            )
            mutator.start()
            result = self._run_profile_manifest(
                output,
                manifest=manifest,
                root=root,
                cli=self.race_cli_path,
                env=environment,
            )
            mutator.join(timeout=30)

            self.assertFalse(mutator.is_alive(), "the source mutator did not stop")
            self.assertFalse(mutation_errors, repr(mutation_errors))
            self.assertTrue(changed.is_set(), "the installed candidate was not observed")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("discovered directory closure changed", result.stderr)
            self.assertNotIn("transaction cleanup failed", result.stderr)
            self.assertEqual(output.read_bytes(), previous)
            self.assertFalse(transient.exists())
            self.assertEqual(drivers.stat().st_mtime_ns, original_times.st_mtime_ns)
            self.assertEqual(self._private_roots(root), set())
            self.assertEqual(list(root.glob(".cupidbuild-old-*")), [])
            self.assertFalse(Path(str(output) + ".cupidbuild.lock").exists())

    def test_typed_profile_manifest_rolls_back_inside_a_replaced_root(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-post-install-root-"
        ) as temporary:
            container = Path(temporary)
            root = container / "repository"
            root.mkdir()
            manifest = self._copy_profile_repository(root)
            output = root / "doom-cupidc-inputs.json"
            previous = b"last known good profile manifest"
            output.write_bytes(previous)
            displaced = container / "displaced-repository"
            ready = container / "publication-ready"
            resume = container / "publication-resume"
            marker = root / "foreign-successor.txt"
            changed = threading.Event()
            blocked = threading.Event()
            mutation_errors = []

            def replace_root_after_candidate_install():
                try:
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline:
                        if ready.is_file():
                            root.rename(displaced)
                            root.mkdir()
                            marker.write_bytes(b"foreign successor")
                            changed.set()
                            return
                        time.sleep(0.001)
                except PermissionError as error:
                    if os.name == "nt":
                        blocked.set()
                    else:
                        mutation_errors.append(error)
                except Exception as error:
                    mutation_errors.append(error)
                finally:
                    try:
                        resume.write_bytes(b"continue")
                    except Exception as error:
                        mutation_errors.append(error)

            environment = os.environ.copy()
            environment["CUPIDBUILD_PUBLICATION_TEST_PHASE"] = "after-install"
            environment["CUPIDBUILD_PUBLICATION_TEST_READY"] = str(ready)
            environment["CUPIDBUILD_PUBLICATION_TEST_RESUME"] = str(resume)
            mutator = threading.Thread(
                target=replace_root_after_candidate_install, daemon=True
            )
            mutator.start()
            result = self._run_profile_manifest(
                output,
                manifest=manifest,
                root=root,
                cli=self.race_cli_path,
                env=environment,
                cwd=container,
            )
            mutator.join(timeout=30)

            self.assertFalse(mutator.is_alive(), "the root mutator did not stop")
            self.assertFalse(mutation_errors, repr(mutation_errors))
            if os.name == "nt":
                self.assertTrue(blocked.is_set(), "the retained root was not observed")
                self.assertFalse(changed.is_set())
                self.assertFalse(displaced.exists())
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(
                    json.loads(output.read_text(encoding="utf-8")),
                    _profile_input_manifest(root),
                )
                self.assertEqual(self._private_roots(root), set())
                return
            self.assertTrue(changed.is_set(), "candidate installation was not observed")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("publication failed", result.stderr)
            self.assertIn("transaction cleanup failed", result.stderr)
            self.assertEqual(marker.read_bytes(), b"foreign successor")
            self.assertEqual(
                (displaced / output.relative_to(root)).read_bytes(), previous
            )
            self.assertNotEqual(self._private_roots(displaced), set())
            marker.unlink()
            root.rmdir()
            displaced.rename(root)

    def test_typed_profile_manifest_preserves_recovery_inside_a_replaced_output_parent(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-post-install-parent-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            stable_parent_lookup = (
                self._retained_directory_lookup_survives_rename(root)
                if os.name != "nt" else None
            )
            manifest = self._copy_profile_repository(root)
            output_parent = root / "artifacts"
            output_parent.mkdir()
            output = output_parent / "doom-cupidc-inputs.json"
            previous = b"last known good profile manifest"
            output.write_bytes(previous)
            os.utime(output, ns=(1_600_000_000_000_000_000,) * 2)
            previous_stat = output.stat()
            displaced = root / "displaced-artifacts"
            ready = root / "publication-ready"
            resume = root / "publication-resume"
            marker = output_parent / "foreign-successor.txt"
            changed = threading.Event()
            blocked = threading.Event()
            mutation_errors = []
            foreign_parent_stat = []

            def replace_parent_after_candidate_install():
                try:
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline:
                        if ready.is_file():
                            output_parent.rename(displaced)
                            output_parent.mkdir()
                            marker.write_bytes(b"foreign successor")
                            foreign_parent_stat.append(output_parent.stat())
                            changed.set()
                            return
                        time.sleep(0.001)
                except PermissionError as error:
                    if os.name == "nt":
                        blocked.set()
                    else:
                        mutation_errors.append(error)
                except Exception as error:
                    mutation_errors.append(error)
                finally:
                    try:
                        resume.write_bytes(b"continue")
                    except Exception as error:
                        mutation_errors.append(error)

            environment = os.environ.copy()
            environment["CUPIDBUILD_PUBLICATION_TEST_PHASE"] = "after-install"
            environment["CUPIDBUILD_PUBLICATION_TEST_READY"] = str(ready)
            environment["CUPIDBUILD_PUBLICATION_TEST_RESUME"] = str(resume)
            mutator = threading.Thread(
                target=replace_parent_after_candidate_install, daemon=True
            )
            mutator.start()
            result = self._run_profile_manifest(
                output,
                manifest=manifest,
                root=root,
                cli=self.race_cli_path,
                env=environment,
            )
            mutator.join(timeout=30)

            self.assertFalse(mutator.is_alive(), "the parent mutator did not stop")
            self.assertFalse(mutation_errors, repr(mutation_errors))
            if os.name == "nt":
                self.assertTrue(blocked.is_set(), "the retained parent was not observed")
                self.assertFalse(changed.is_set())
                self.assertFalse(displaced.exists())
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(
                    json.loads(output.read_text(encoding="utf-8")),
                    _profile_input_manifest(root),
                )
                self.assertEqual(self._private_roots(root), set())
                return
            self.assertTrue(changed.is_set(), "candidate installation was not observed")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("publication failed", result.stderr)
            self.assertIn("transaction cleanup failed", result.stderr)
            self.assertEqual(marker.read_bytes(), b"foreign successor")
            self._assert_replaced_parent_recovery(
                root, output, displaced, previous, previous_stat,
                stable_parent_lookup, foreign_parent_stat[0],
            )
            if not stable_parent_lookup:
                self.assertEqual(
                    json.loads((displaced / output.name).read_text(encoding="utf-8")),
                    _profile_input_manifest(root),
                )
            marker.unlink()
            output_parent.rmdir()
            displaced.rename(output_parent)

    def _retained_directory_lookup_survives_rename(self, container):
        """Probe the fixture filesystem without depending on a mount name."""
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-dirfd-capability-", dir=container
        ) as temporary:
            root = Path(temporary)
            parent = root / "parent"
            parent.mkdir()
            (parent / "entry").write_bytes(b"owned directory entry")
            descriptor = os.open(
                parent, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC
            )
            try:
                parent.rename(root / "displaced")
                parent.mkdir()
                (parent / "entry").write_bytes(b"foreign directory entry")
                try:
                    entry = os.open(
                        "entry", os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC,
                        dir_fd=descriptor,
                    )
                except OSError as error:
                    if error.errno == errno.ENOENT:
                        return False
                    raise
                try:
                    payload = os.read(entry, 100)
                finally:
                    os.close(entry)
                self.assertIn(
                    payload, (b"owned directory entry", b"foreign directory entry"),
                    "retained-directory probe returned an unexpected entry",
                )
                return payload == b"owned directory entry"
            finally:
                os.close(descriptor)

    def _assert_replaced_parent_recovery(
        self, root, output, displaced, previous, previous_stat,
        stable_parent_lookup, foreign_parent_stat,
    ):
        current_parent_stat = output.parent.stat()
        self.assertEqual(
            (current_parent_stat.st_dev, current_parent_stat.st_ino,
             current_parent_stat.st_mtime_ns),
            (foreign_parent_stat.st_dev, foreign_parent_stat.st_ino,
             foreign_parent_stat.st_mtime_ns),
            "publication changed the foreign replacement directory identity or mtime",
        )
        self.assertEqual(
            {path.name for path in output.parent.iterdir()},
            {"foreign-successor.txt"},
            "publication changed the foreign replacement directory",
        )
        if stable_parent_lookup:
            recovered = displaced / output.name
        else:
            backups = list(displaced.glob(".cupidbuild-old-*"))
            self.assertEqual(len(backups), 1, "the recovery backup was not retained")
            recovered = backups[0]
        self.assertFalse(recovered.is_symlink())
        recovered_stat = recovered.stat()
        self.assertTrue(stat.S_ISREG(recovered_stat.st_mode))
        self.assertEqual(recovered.read_bytes(), previous)
        self.assertEqual(recovered_stat.st_mtime_ns, previous_stat.st_mtime_ns)
        self.assertEqual(
            (recovered_stat.st_dev, recovered_stat.st_ino),
            (previous_stat.st_dev, previous_stat.st_ino),
        )
        private_roots = self._private_roots(root)
        self.assertEqual(len(private_roots), 1)
        self.assertTrue(
            self._private_entry(next(iter(private_roots)), "candidate.o").is_file()
        )
        self.assertTrue((displaced / (output.name + ".cupidbuild.lock")).is_file())

    @unittest.skipUnless(os.name == "nt", "old-output disposition is Windows-only")
    def test_typed_profile_manifest_keeps_a_committed_candidate_when_old_cleanup_fails(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-disposition-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            output = root / "doom-cupidc-inputs.json"
            previous = b"last known good profile manifest"
            output.write_bytes(previous)
            environment = os.environ.copy()
            environment[
                "CUPIDBUILD_PUBLICATION_TEST_FAIL_OLD_DISPOSITION"
            ] = "1"

            result = self._run_profile_manifest(
                output,
                manifest=manifest,
                root=root,
                cli=self.race_cli_path,
                env=environment,
            )

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn(
                "profile manifest was published, but transaction cleanup "
                "was incomplete",
                result.stderr,
            )
            self.assertNotEqual(output.read_bytes(), previous)
            self.assertEqual(
                json.loads(output.read_text(encoding="utf-8"))["schema"],
                "cupid.doom-profile-inputs.v1",
            )
            backups = list(root.glob(".cupidbuild-old-*"))
            self.assertEqual(len(backups), 1)
            self.assertEqual(backups[0].read_bytes(), previous)

    def test_typed_profile_manifest_rejects_an_output_alias_of_an_input(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-alias-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            header = root / "drivers" / "ata.h"
            original = header.read_bytes()
            output = root / "doom-cupidc-inputs.json"
            os.link(header, output)

            result = self._run_profile_manifest(output, manifest=manifest, root=root)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("output may not replace an input", result.stderr)
            self.assertEqual(output.read_bytes(), original)
            self.assertEqual(header.read_bytes(), original)
            self.assertEqual(self._private_roots(root), set())

    def test_typed_profile_manifest_parity_failure_preserves_previous_output(
        self,
    ):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-parity-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_checked_assembly_seed(root / "seed")
            document = json.loads(manifest.read_text(encoding="utf-8"))
            artifact = next(
                item for item in document["artifacts"] if item["name"] == "cupidobj"
            )
            cupidobj = manifest.parent / artifact["file"]
            payload = cupidobj.read_bytes()
            profile_key = b'"profiles":'
            self.assertEqual(payload.count(profile_key), 1)
            self._replace_seed_tool_bytes(
                manifest,
                "cupidobj",
                payload.replace(profile_key, b'"profilet":'),
            )
            output = root / "doom-cupidc-inputs.json"
            output.write_bytes(b"last known good profile manifest")
            before = self._private_roots()

            result = self._run_profile_manifest(output, manifest=manifest)

            self.assertNotEqual(result.returncode, 0)
            self.assertIn("differs from the independent renderer", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good profile manifest")
            self.assertEqual(self._private_roots(), before)

    def test_typed_profile_manifest_input_drift_preserves_previous_output(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-input-drift-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            header = root / "drivers" / "ata.h"
            output = root / "doom-cupidc-inputs.json"
            output.write_bytes(b"last known good profile manifest")
            changed = threading.Event()

            def change_header_after_snapshot_is_frozen():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    if any(
                        self._private_candidate_has_bytes(token)
                        for token in self._private_roots(root)
                    ):
                        header.write_bytes(header.read_bytes() + b"\n")
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=change_header_after_snapshot_is_frozen, daemon=True
            )
            mutator.start()
            result = self._run_profile_manifest(output, manifest=manifest, root=root)
            mutator.join(timeout=20)

            self.assertTrue(changed.is_set(), "the frozen snapshot was not observed")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("inputs changed", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good profile manifest")
            self.assertEqual(self._private_roots(root), set())

    def test_typed_profile_manifest_seed_drift_preserves_previous_output(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-seed-drift-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            output = root / "doom-cupidc-inputs.json"
            output.write_bytes(b"last known good profile manifest")
            changed = threading.Event()

            def change_seed_after_snapshot_is_frozen():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    if any(
                        self._private_candidate_has_bytes(token)
                        for token in self._private_roots(root)
                    ):
                        manifest.write_bytes(manifest.read_bytes() + b" \n")
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=change_seed_after_snapshot_is_frozen, daemon=True
            )
            mutator.start()
            result = self._run_profile_manifest(output, manifest=manifest, root=root)
            mutator.join(timeout=20)

            self.assertTrue(changed.is_set(), "the frozen snapshot was not observed")
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("checked seed inputs changed", result.stderr)
            self.assertEqual(output.read_bytes(), b"last known good profile manifest")
            self.assertEqual(self._private_roots(root), set())

    def test_typed_profile_manifest_output_drift_is_not_overwritten(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-output-drift-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            output = root / "doom-cupidc-inputs.json"
            output.write_bytes(b"last known good profile manifest")
            changed = threading.Event()
            blocked = threading.Event()

            def change_output_after_snapshot_is_frozen():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    if any(
                        self._private_candidate_has_bytes(token)
                        for token in self._private_roots(root)
                    ):
                        try:
                            output.write_bytes(b"concurrent profile manifest")
                        except PermissionError:
                            blocked.set()
                            return
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=change_output_after_snapshot_is_frozen, daemon=True
            )
            mutator.start()
            result = self._run_profile_manifest(output, manifest=manifest, root=root)
            mutator.join(timeout=20)

            if os.name == "nt":
                self.assertTrue(blocked.is_set(), "the retained output was not observed")
                self.assertFalse(changed.is_set())
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(
                    json.loads(output.read_text(encoding="utf-8")),
                    _profile_input_manifest(root),
                )
            else:
                self.assertTrue(changed.is_set(), "the frozen snapshot was not observed")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("output changed", result.stderr)
                self.assertEqual(output.read_bytes(), b"concurrent profile manifest")
            self.assertEqual(self._private_roots(root), set())

    def test_typed_profile_manifest_lock_drift_preserves_previous_output(self):
        with tempfile.TemporaryDirectory(
            prefix=".cupidbuild-profile-lock-drift-", dir=REPO_ROOT
        ) as temporary:
            root = Path(temporary)
            manifest = self._copy_profile_repository(root)
            output = root / "doom-cupidc-inputs.json"
            lock = Path(str(output) + ".cupidbuild.lock")
            output.write_bytes(b"last known good profile manifest")
            changed = threading.Event()
            blocked = threading.Event()

            def change_lock_after_snapshot_is_frozen():
                deadline = time.monotonic() + 20
                while time.monotonic() < deadline:
                    if lock.is_file() and any(
                        self._private_candidate_has_bytes(token)
                        for token in self._private_roots(root)
                    ):
                        try:
                            lock.write_bytes(b"4294967295\n")
                        except PermissionError:
                            blocked.set()
                            return
                        changed.set()
                        return
                    time.sleep(0.001)

            mutator = threading.Thread(
                target=change_lock_after_snapshot_is_frozen, daemon=True
            )
            mutator.start()
            result = self._run_profile_manifest(output, manifest=manifest, root=root)
            mutator.join(timeout=20)

            if os.name == "nt":
                self.assertTrue(blocked.is_set(), "the retained lock was not observed")
                self.assertFalse(changed.is_set())
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(
                    json.loads(output.read_text(encoding="utf-8")),
                    _profile_input_manifest(root),
                )
                self.assertFalse(lock.exists())
            else:
                self.assertTrue(changed.is_set(), "the frozen snapshot was not observed")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("lock changed", result.stderr)
                self.assertEqual(output.read_bytes(), b"last known good profile manifest")
                self.assertTrue(lock.is_file())
            self.assertEqual(self._private_roots(root), set())

    def test_typed_profile_manifest_runs_cupidobj_before_independent_parity(self):
        source = (TOOLCHAIN_ROOT / "cupidbuild_compile_profile.inc").read_text(
            encoding="utf-8"
        )
        operation = source.split("int cupidbuild_generate_profile_manifest(", 1)[
            1
        ].split("static void cupidbuild_compile_put_u32(", 1)[0]

        object_transform = operation.index("cupidbuild_host_run_in_private(")
        independent_render = operation.index("cupidbuild_profile_render_json(")
        parity = operation.index("memcmp(candidate, expected.bytes")
        membership_recheck = operation.index("cupidbuild_profile_require_membership(")
        publication = operation.index("cupidbuild_host_publish_if_changed(")

        self.assertLess(object_transform, independent_render)
        self.assertLess(independent_render, parity)
        self.assertLess(parity, membership_recheck)
        self.assertLess(membership_recheck, publication)
        self.assertEqual(operation.count("cupidbuild_profile_render_json("), 1)

    def test_typed_profile_manifest_binds_directory_tokens_to_publication(self):
        source = (TOOLCHAIN_ROOT / "cupidbuild_compile_profile.inc").read_text(
            encoding="utf-8"
        )
        host = (TOOLCHAIN_ROOT / "cupidbuild_host.cc").read_text(encoding="utf-8")
        operation = source.split("int cupidbuild_generate_profile_manifest(", 1)[
            1
        ].split("static void cupidbuild_compile_put_u32(", 1)[0]
        equality_start = _c_spelling_ranges(
            host, "int cupidbuild_host_snapshot_equal("
        )[0][0]
        equality_end = _c_spelling_ranges(
            host, "static int cupidbuild_host_snapshot_identity_equal("
        )[0][0]
        equality = host[equality_start:equality_end]
        boundary = host.split(
            "static int cupidbuild_host_require_public_binding(", 1
        )[1].split("int cupidbuild_host_require_publication_boundary(", 1)[0]
        closure_start = _c_spelling_ranges(
            host, "static int cupidbuild_host_require_discovery_directories("
        )[0][0]
        closure_end = _c_spelling_ranges(
            host, "static int cupidbuild_host_write_lock_exclusive("
        )[0][0]
        closure = host[closure_start:closure_end]
        validation = "cupidbuild_host_require_discovery_directory_pass(transaction)"
        first_validation = closure.index(validation)
        after_first_pass = closure.index(
            "CUPIDBUILD_PROFILE_TEST_DIRECTORY_AFTER_FIRST_PASS_READY"
        )
        second_validation = closure.index(validation, first_validation + 1)

        self.assertIn("memcmp(left->changed, right->changed", equality)
        self.assertIn("cupidbuild_host_seal_discovery(transaction)", operation)
        self.assertIn("cupidbuild_host_require_discovery_directories", boundary)
        self.assertIn("transaction->discovery_boundary_count++", closure)
        self.assertNotIn("static unsigned int boundary_count", closure)
        self.assertEqual(closure.count(validation), 2)
        self.assertLess(first_validation, after_first_pass)
        self.assertLess(after_first_pass, second_validation)
        self.assertIn("cupidbuild_host_bind_discovery_directory", host)
        self.assertIn("cupidbuild_host_close_discovery_directories", host)
        self.assertIn("information + 32u", host)
        self.assertIn("information + 36u", host)
        self.assertIn("opened.changed[0] = after.change_high", host)
        self.assertIn("opened.changed[1] = after.change_low", host)

    def test_typed_profile_manifest_prepares_only_its_fixed_parent_chain(self):
        source = (TOOLCHAIN_ROOT / "cupidbuild_compile_profile.inc").read_text(
            encoding="utf-8"
        )
        entry = (TOOLCHAIN_ROOT / "cupidbuild.cc").read_text(encoding="utf-8")
        host = (TOOLCHAIN_ROOT / "cupidbuild_host.cc").read_text(encoding="utf-8")
        operation = source.split("int cupidbuild_generate_profile_manifest(", 1)[
            1
        ].split("static void cupidbuild_compile_put_u32(", 1)[0]

        preparation = operation.index("cupidbuild_host_profile_parent_prepare(")
        transaction = operation.index("cupidbuild_host_profile_transaction_open(")
        publication = operation.index("cupidbuild_host_publish_if_changed(")
        commit = operation.index("cupidbuild_host_profile_parent_commit(")
        transaction_close = operation.index("cupidbuild_finish_publication(")
        parent_close = operation.index("cupidbuild_host_profile_parent_close(")

        self.assertIn(
            '#define CUPIDBUILD_PROFILE_OUTPUT '
            '"build/bootstrap/doom-cupidc-inputs.json"',
            entry,
        )
        self.assertLess(preparation, transaction)
        self.assertLess(publication, commit)
        self.assertLess(transaction_close, parent_close)
        self.assertIn("cupidbuild_host_transaction_close(transaction)", entry)
        self.assertIn("CUPIDBUILD_WINDOWS_FILE_CREATE", host)
        self.assertIn("attributes.root_directory = parent", host)
        self.assertIn("CUPIDBUILD_LINUX_SYS_MKDIRAT", host)
        self.assertIn("mkdirat(parent, name, 0700)", host)
        self.assertIn("cupidbuild_host_profile_parent_open_root", host)
        self.assertIn("cupidbuild_host_snapshot_identity_equal", host)
        self.assertIn("preparation->bootstrap_created", host)
        self.assertIn("preparation->build_created", host)
        self.assertLess(
            host.index("if (preparation->bootstrap_created != 0"),
            host.index("if (preparation->build_created != 0"),
        )

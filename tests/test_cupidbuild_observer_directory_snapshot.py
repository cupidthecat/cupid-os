"""Retained directory discovery through native or supplied checked callers.

Set CUPIDBUILD_OBSERVER_DIRECTORY_SNAPSHOT_PROGRAM to use a supplied ordinary
caller. A separate CUPIDBUILD_OBSERVER_DIRECTORY_SNAPSHOT_FAULT_PROGRAM must
include CUPIDBUILD_DIRECTORY_SNAPSHOT_TEST in both caller and host adapter.
Supplying only the ordinary caller skips fault tests without compiling a native
substitute. CUPIDBUILD_OBSERVER_DIRECTORY_SNAPSHOT_PRODUCTS selects an existing
outside-repository directory; its exclusive run child retains fixtures, programs
and per-command receipts, including failures. The default temporary run cleans
up when the class closes. None of these tests publishes an SDK cohort.
"""
import json
import os
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROGRAM_ENV = "CUPIDBUILD_OBSERVER_DIRECTORY_SNAPSHOT_PROGRAM"
FAULT_ENV = "CUPIDBUILD_OBSERVER_DIRECTORY_SNAPSHOT_FAULT_PROGRAM"
PRODUCTS_ENV = "CUPIDBUILD_OBSERVER_DIRECTORY_SNAPSHOT_PRODUCTS"


class CupidBuildObserverDirectorySnapshotTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        retained = os.environ.get(PRODUCTS_ENV)
        if retained:
            parent = Path(retained).resolve(strict=True)
            if not parent.is_dir() or parent == ROOT or ROOT in parent.parents:
                raise ValueError("snapshot products must be outside the repository")
            cls.products = Path(tempfile.mkdtemp(prefix="observer-directory-", dir=parent))
        else:
            temporary = tempfile.TemporaryDirectory(prefix="observer-directory-")
            cls.addClassCleanup(temporary.cleanup)
            cls.products = Path(temporary.name)
        cls.sequence = 0
        selected = os.environ.get(PROGRAM_ENV)
        cls.program = Path(selected).resolve(strict=True) if selected else cls.build(False)
        selected_fault = os.environ.get(FAULT_ENV)
        cls.fault_program = (Path(selected_fault).resolve(strict=True) if selected_fault
                             else None if selected else cls.build(True))

    @classmethod
    def receipt(cls, command, returncode, stdout, stderr, error=None):
        cls.sequence += 1
        record = {
            "argv": [str(part) for part in command],
            "returncode": returncode,
            "stdout_hex": stdout.hex(),
            "stderr_hex": stderr.hex(),
            "error": error,
        }
        target = cls.products / f"command-{cls.sequence:04d}.json"
        with target.open("xb") as stream:
            stream.write((json.dumps(record, sort_keys=True, separators=(",", ":"))
                          + "\n").encode("ascii"))

    @classmethod
    def command(cls, command, timeout):
        try:
            result = subprocess.run(command, capture_output=True, timeout=timeout)
        except subprocess.TimeoutExpired as error:
            cls.receipt(command, None, error.stdout or b"", error.stderr or b"",
                        "TimeoutExpired")
            raise
        except OSError as error:
            cls.receipt(command, None, b"", b"", type(error).__name__ + ": " + str(error))
            raise
        cls.receipt(command, result.returncode, result.stdout, result.stderr)
        return result

    @classmethod
    def build(cls, fault):
        requested = os.environ.get("CC", "clang" if os.name == "nt" else "cc")
        compiler = shutil.which(requested)
        if not compiler:
            raise RuntimeError("native snapshot observer compiler unavailable: " + requested)
        config = ["--no-default-config"] if Path(compiler).resolve(strict=True).name.lower().startswith("clang") else []
        program = cls.products / (("fault" if fault else "ordinary")
                                  + (".exe" if os.name == "nt" else ""))
        command = [compiler, *config, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                   "-D_CRT_SECURE_NO_WARNINGS", "-I", str(ROOT / "toolchain"),
                   *(["-DCUPIDBUILD_DIRECTORY_SNAPSHOT_TEST=1"] if fault else []),
                   "-x", "c",
                   str(ROOT / "toolchain/tests/cupidbuild_observer_directory_snapshot_contract.cc"),
                   str(ROOT / "toolchain/cupidbuild_host.cc"),
                   str(ROOT / "toolchain/path_encoding.cc"),
                   *(["-lntdll"] if os.name == "nt" else []), "-o", str(program)]
        result = cls.command(command, 180)
        if result.returncode or result.stdout or result.stderr:
            raise AssertionError((result.returncode, result.stdout, result.stderr))
        return program

    def setUp(self):
        self.root = Path(tempfile.mkdtemp(prefix="fixture-", dir=self.products))
        self.nested = self.root / "nested"
        self.nested.mkdir()

    def argv(self, mode, logical="nested", fault=False):
        program = self.fault_program if fault else self.program
        if program is None:
            self.skipTest("supplied ordinary caller has no separate fault caller")
        encoded = logical if isinstance(logical, bytes) else logical.encode("utf-8")
        return [str(program), str(self.root).encode("utf-8").hex(), mode, encoded.hex()]

    def run_case(self, mode="list", logical="nested", expected=None, fault=False):
        before = sorted(path.relative_to(self.root).as_posix() for path in self.root.rglob("*"))
        result = self.command(self.argv(mode, logical, fault), 120)
        self.assertEqual((result.returncode, result.stderr), (0, b""), result.stdout)
        if expected is None:
            self.assertEqual(result.stdout, b"")
        else:
            names = sorted(name.encode("utf-8") for name in expected)
            self.assertEqual(result.stdout.splitlines(),
                             [f"count {len(names)}".encode("ascii"),
                              *(name.hex().encode("ascii") for name in names)])
        self.assertEqual(before, sorted(path.relative_to(self.root).as_posix()
                                        for path in self.root.rglob("*")))

    def mutate(self, action, mode="wait", logical="nested", fault=False):
        command = self.argv(mode, logical, fault)
        try:
            process = subprocess.Popen(command, stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        except OSError as failure:
            self.receipt(command, None, b"", b"", type(failure).__name__ + ": " + str(failure))
            raise
        ready = queue.Queue()
        reader = threading.Thread(target=lambda: ready.put(process.stdout.readline()), daemon=True)
        reader.start()
        prefix = None
        stdout = b""
        stderr = b""
        error = None
        communicated = False
        try:
            prefix = ready.get(timeout=30)
            reader.join(timeout=1)
            self.assertFalse(reader.is_alive())
            self.assertEqual(prefix.replace(b"\r\n", b"\n"), b"ready\n")
            unchanged = action()
            stdout, stderr = process.communicate(b"p" if unchanged else b"f", timeout=120)
            communicated = True
            self.assertEqual((process.returncode, stdout, stderr), (0, b"", b""))
        except BaseException as failure:
            error = type(failure).__name__ + ": " + str(failure)
            raise
        finally:
            try:
                if not communicated:
                    if process.poll() is None:
                        process.kill()
                    reader.join(timeout=1)
                    if prefix is None:
                        try:
                            prefix = ready.get_nowait()
                        except queue.Empty:
                            pass
                    if reader.is_alive():
                        error = (error or "") + "; stdout prefix reader did not stop"
                    self.assertFalse(reader.is_alive())
                    stdout, stderr = process.communicate(timeout=30)
            finally:
                self.receipt(command, process.returncode, (prefix or b"") + stdout, stderr, error)
                for stream in (process.stdin, process.stdout, process.stderr):
                    if stream is not process.stdout or not reader.is_alive():
                        stream.close()

    def fill(self, directory, count):
        for index in range(count):
            (directory / f"f{index:04d}").touch()

    def test_empty_directory_and_root(self):
        self.run_case(expected=[])
        self.nested.rmdir()
        self.run_case(logical="", expected=[])

    def test_literal_names_sorted_without_suffix_or_kind_filtering(self):
        names = ["z.h", ".hidden", "b.inc", "plain", "directory.h"]
        for name in names[:-1]:
            (self.nested / name).touch()
        (self.nested / names[-1]).mkdir()
        self.run_case(expected=names)

    def test_unicode_and_supplementary_names(self):
        names = ["z", "caf\u00e9", "\u8cc7\u6599-\U0001f431", "A"]
        for name in names:
            (self.nested / name).touch()
        self.run_case(expected=names)
        renamed = self.root / "\u89aa-\U0001f431"
        self.nested.rename(renamed)
        self.run_case(logical=renamed.name, expected=names)

    def test_token_survives_replaced_root_members_storage_and_new_token(self):
        (self.root / "a").touch()
        self.run_case("repeat-root", "", expected=["a", "nested"])

    def test_existing_unordered_expected_membership_can_precede_capture(self):
        for name in ("a", "b"):
            (self.nested / name).touch()
        self.run_case("expected-first", expected=["a", "b"])

    def test_invalid_arguments_clear_outputs_and_poison(self):
        for mode in ("null-observer", "null-out", "null-path", "null-token",
                     "null-names", "null-count", "unknown-token", "foreign", "prepoison"):
            with self.subTest(mode=mode):
                self.run_case(mode)

    def test_invalid_paths_and_utf8(self):
        for logical in ("/nested", "../nested", "nested/.", "nested/..", "nested//bad",
                        "nested/", "nested\\name", "nested:name", "a" * 1024,
                        b"\xc0\xaf", b"\xed\xa0\x80", b"\xf4\x90\x80\x80", b"\xe2\x82"):
            with self.subTest(logical=logical):
                self.run_case("reject", logical)

    def test_missing_and_regular_file_are_not_directories(self):
        (self.nested / "file").touch()
        self.run_case("reject", "absent")
        self.run_case("reject", "nested/file")
        self.run_case("reject", "nested/file/child")

    def test_added_removed_and_renamed_member_invalidate_view(self):
        (self.nested / "a").touch()
        self.mutate(lambda: ((self.nested / "b").touch(), False)[1])
        self.mutate(lambda: ((self.nested / "b").unlink(), False)[1])
        self.mutate(lambda: ((self.nested / "a").rename(self.nested / "c"), False)[1])

    def test_changed_membership_cannot_be_reenrolled(self):
        self.mutate(lambda: ((self.nested / "added").touch(), False)[1], mode="wait-recapture")
        self.mutate(lambda: ((self.nested / "second").touch(), False)[1], mode="wait-expected")

    def test_unchanged_membership_does_not_claim_child_kind_or_payload(self):
        (self.nested / "payload").write_bytes(b"one")
        self.mutate(lambda: ((self.nested / "payload").write_bytes(b"two"), True)[1])
        def replace_kind():
            (self.nested / "payload").unlink()
            (self.nested / "payload").mkdir()
            return True
        self.mutate(replace_kind)

    def test_accessor_checks_other_captured_payload_with_restored_time(self):
        target = self.nested / "payload"
        target.write_bytes(b"one")
        def overwrite():
            before = target.stat()
            target.write_bytes(b"two")
            os.utime(target, ns=(before.st_atime_ns, before.st_mtime_ns))
            return False
        self.mutate(overwrite, mode="wait-payload")

    def test_original_parent_binding_and_root_metadata_remain_required(self):
        def replace():
            try:
                self.nested.rename(self.root / "parked")
            except PermissionError:
                self.assertEqual(os.name, "nt")
                return True
            self.nested.mkdir()
            return False
        self.mutate(replace)
        def metadata():
            status = self.root.stat()
            os.utime(self.root, ns=(status.st_atime_ns, status.st_mtime_ns + 2_000_000_000))
            return False
        self.mutate(metadata)

    def test_token_budget_includes_empty_snapshots(self):
        self.nested.rmdir()
        self.run_case("token-limit", "")

    def test_many_small_snapshots_keep_the_first_view_and_hit_exact_copy_budget(self):
        self.nested.rmdir()
        (self.root / "only").touch()
        self.run_case("single-copy-limit", "")

    def test_aggregate_name_copy_budget_is_independent_of_handle_count(self):
        self.nested.rmdir()
        self.fill(self.root, 2048)
        self.run_case("copy-limit", "")

    def test_full_name_limit_and_one_over(self):
        self.fill(self.nested, 4096)
        self.run_case(expected=[f"f{index:04d}" for index in range(4096)])
        (self.nested / "overflow").touch()
        self.run_case("reject")

    def test_existing_membership_consumes_its_own_name_budget(self):
        self.fill(self.nested, 4096)
        self.run_case("membership-limit")

    @unittest.skipIf(os.name == "nt", "POSIX names and FIFO")
    def test_existing_portable_profile_rejects_colon_backslash_and_invalid_utf8(self):
        for name in (b"colon:name", b"back\\slash", b"\xff"):
            with self.subTest(name=name):
                target = os.fsencode(self.nested) + b"/" + name
                with open(target, "xb"):
                    pass
                try:
                    self.run_case("reject")
                finally:
                    os.unlink(target)

    @unittest.skipIf(os.name == "nt", "POSIX control-character names")
    def test_admitted_control_character_names_keep_literal_spelling(self):
        names = ["line\nbreak", "tab\tname"]
        for name in names:
            (self.nested / name).touch()
        self.run_case(expected=names)

    @unittest.skipIf(os.name == "nt", "POSIX symbolic link and FIFO")
    def test_link_and_fifo_child_names_do_not_infer_kinds(self):
        (self.nested / "link").symlink_to(self.root, target_is_directory=True)
        os.mkfifo(self.nested / "fifo")
        self.run_case(expected=["fifo", "link"])
        self.run_case("reject", "nested/link")
        self.run_case("reject", "nested/fifo")

    def test_fault_allocation_steps_poison_and_fresh_capture_recovers(self):
        for name in ("a", "b"):
            (self.nested / name).touch()
        for step in range(8):
            with self.subTest(step=step):
                self.run_case(f"fault-alloc-{step}", fault=True)

    def test_fault_enumeration_completion_and_postbinding_cleanup(self):
        for name in ("a", "b"):
            (self.nested / name).touch()
        for mode in ("fault-enumerate", "fault-complete", "fault-bound"):
            with self.subTest(mode=mode):
                self.run_case(mode, fault=True)

    def test_fault_capture_boundary_rejects_new_member_before_token_publication(self):
        self.mutate(lambda: ((self.nested / "added").touch(), False)[1],
                    mode="fault-capture-wait", fault=True)


if __name__ == "__main__":
    unittest.main()

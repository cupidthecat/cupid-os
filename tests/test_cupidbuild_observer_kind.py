"""Retained kind discovery with native or supplied Cupid-built callers."""
import os
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CupidBuildObserverKindTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        selected = os.environ.get("CUPIDBUILD_OBSERVER_KIND_PROGRAM")
        if selected:
            cls.program = Path(selected).resolve(strict=True)
            return
        temporary = tempfile.TemporaryDirectory(prefix="observer-kind-build-")
        cls.addClassCleanup(temporary.cleanup)
        cls.program = Path(temporary.name) / ("caller.exe" if os.name == "nt" else "caller")
        command = [shutil.which("clang" if os.name == "nt" else "cc"), "-std=c11", "-O2",
                   "-Wall", "-Wextra", "-Werror", "-D_CRT_SECURE_NO_WARNINGS",
                   "-I", str(ROOT / "toolchain"), "-x", "c",
                   str(ROOT / "toolchain/tests/cupidbuild_observer_kind_contract.cc"),
                   str(ROOT / "toolchain/cupidbuild_host.cc"),
                   str(ROOT / "toolchain/path_encoding.cc"),
                   *(["-lntdll"] if os.name == "nt" else []), "-o", str(cls.program)]
        result = subprocess.run(command, capture_output=True, timeout=180)
        if result.returncode:
            raise AssertionError(result.stderr.decode(errors="replace"))

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="observer-kind-fixture-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / "nested").mkdir()
        (self.root / "nested/file").write_bytes(b"one")
        (self.root / "nested/empty-file").touch()
        (self.root / "nested/empty-directory").mkdir()
        (self.root / "nested/資料-😀").mkdir()
        (self.root / "nested/資料-😀/café").write_bytes(b"unicode")

    def run_case(self, mode, logical):
        result = subprocess.run([str(self.program), str(self.root).encode("utf-8").hex(),
                                 mode, logical.encode("utf-8").hex()],
                                capture_output=True, timeout=15)
        self.assertEqual((result.returncode, result.stdout, result.stderr), (0, b"", b""))

    def mutate(self, mode, logical, action):
        process = subprocess.Popen([str(self.program), str(self.root).encode("utf-8").hex(),
                                    mode, logical.encode("utf-8").hex()],
                                   stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        ready = queue.Queue()
        threading.Thread(target=lambda: ready.put(process.stdout.readline()), daemon=True).start()
        try:
            self.assertEqual(ready.get(timeout=15).replace(b"\r\n", b"\n"), b"ready\n")
            expected = action()
            stdout, stderr = process.communicate(b"p" if expected else b"f", timeout=15)
            self.assertEqual((process.returncode, stdout, stderr), (0, b"", b""))
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()

    def test_files_and_directories_including_empty_and_root(self):
        for mode, logical in (("directory", ""), ("directory", "nested"),
                              ("directory", "nested/empty-directory"),
                              ("file", "nested/file"), ("file", "nested/empty-file")):
            with self.subTest(logical=logical):
                self.run_case(mode, logical)

    def test_unicode_components(self):
        self.run_case("directory", "nested/資料-😀")
        self.run_case("file", "nested/資料-😀/café")

    def test_full_flat_and_maximum_depth_iso_inventories(self):
        original = self.root
        for case, count in (("flat", 512), ("deep", 505)):
            with self.subTest(case=case):
                self.root = original / case
                self.root.mkdir()
                target = self.root
                if case == "deep":
                    for depth in range(7):
                        target = target / ("d" + str(depth))
                        target.mkdir()
                for index in range(count):
                    (target / f"f{index:03d}.bin").write_bytes(b"x")
                self.run_case("tree", case)

    def test_invalid_arguments_clear_results_and_poison(self):
        for mode in ("null-observer", "null-path", "null-out", "bad-utf8"):
            with self.subTest(mode=mode):
                self.run_case(mode, "nested/file")

    def test_invalid_paths_missing_and_non_directory_parent(self):
        for logical in ("/nested", "../nested", "nested/../file", "nested//file",
                        "nested/./file", "nested\\file", "nested:file", "nested/",
                        "missing", "nested/file/child", "nested/" + "a" * 1024):
            with self.subTest(logical=logical):
                self.run_case("reject", logical)

    def test_existing_typed_operations_still_require_their_kind(self):
        self.run_case("file-strict", "nested/empty-directory")
        self.run_case("directory-strict", "nested/file")

    def test_kind_retains_regular_file_metadata(self):
        def change():
            (self.root / "nested/file").write_bytes(b"longer")
            return False
        self.mutate("wait", "nested/file", change)

    def test_directory_kind_does_not_capture_membership(self):
        def change():
            (self.root / "nested/empty-directory/added").write_bytes(b"new")
            return True
        self.mutate("wait", "nested/empty-directory", change)

    def test_explicit_membership_after_kind_retains_drift(self):
        def change():
            (self.root / "nested/empty-directory/added").write_bytes(b"new")
            return False
        self.mutate("members", "nested/empty-directory", change)

    def test_explicit_payload_after_kind_detects_restored_metadata_edit(self):
        def change():
            path = self.root / "nested/file"
            before = path.stat()
            path.write_bytes(b"two")
            os.utime(path, ns=(before.st_atime_ns, before.st_mtime_ns))
            return False
        self.mutate("payload", "nested/file", change)

    def test_original_file_binding_is_retained(self):
        def replace():
            path = self.root / "nested/file"
            try:
                path.rename(path.with_name("old"))
            except PermissionError:
                self.assertEqual(os.name, "nt")
                return True
            path.mkdir()
            return False
        self.mutate("wait", "nested/file", replace)

    def test_original_directory_binding_is_retained(self):
        def replace():
            path = self.root / "nested/empty-directory"
            try:
                path.rename(path.with_name("old"))
            except PermissionError:
                self.assertEqual(os.name, "nt")
                return True
            path.write_bytes(b"replacement")
            return False
        self.mutate("wait", "nested/empty-directory", replace)

    def test_repeated_walk_rejects_changed_ancestor_before_recapture(self):
        def replace():
            parent = self.root / "nested"
            try:
                parent.rename(self.root / "old")
            except PermissionError:
                self.assertEqual(os.name, "nt")
                return True
            parent.mkdir()
            (parent / "file").write_bytes(b"one")
            return False
        self.mutate("repeat", "nested/file", replace)

    @unittest.skipIf(os.name == "nt", "POSIX FIFO and symbolic links")
    def test_links_and_special_file_fail_without_blocking(self):
        (self.root / "nested/file-link").symlink_to(self.root / "nested/file")
        (self.root / "nested/directory-link").symlink_to(self.root / "nested/empty-directory", target_is_directory=True)
        os.mkfifo(self.root / "nested/fifo")
        for logical in ("nested/file-link", "nested/directory-link", "nested/directory-link/child", "nested/fifo"):
            self.run_case("reject", logical)

    @unittest.skipUnless(os.name == "nt", "Windows directory junction")
    def test_junction_and_its_child_fail(self):
        junction = self.root / "nested/junction"
        result = subprocess.run(["cmd", "/c", "mklink", "/J", str(junction), str(self.root / "nested")],
                                capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.run_case("reject", "nested/junction")
        self.run_case("reject", "nested/junction/file")


if __name__ == "__main__":
    unittest.main()

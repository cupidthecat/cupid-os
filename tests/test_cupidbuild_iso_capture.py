"""Retained ISO input capture with native or supplied Cupid-built callers."""
import concurrent.futures
import os
from pathlib import Path
import queue
import shutil
import subprocess
import tempfile
import threading
import unittest

from tools import hostbuild

ROOT = Path(__file__).resolve().parents[1]


def digest(data):
    value = 2166136261
    for byte in data:
        value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


class CupidBuildIsoCaptureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        selected = os.environ.get("CUPIDBUILD_ISO_CAPTURE_PROGRAM")
        if selected:
            cls.program = Path(selected).resolve(strict=True)
            return
        temporary = tempfile.TemporaryDirectory(prefix="iso-capture-build-")
        cls.addClassCleanup(temporary.cleanup)
        cls.program = Path(temporary.name) / ("caller.exe" if os.name == "nt" else "caller")
        compiler = shutil.which("clang" if os.name == "nt" else "cc")
        if compiler is None:
            raise AssertionError("Native contract compiler is unavailable")
        names = ("tests/cupidbuild_iso_capture_contract.cc", "cupidbuild_iso_capture.cc",
                 "cupidbuild_iso.cc", "cupidbuild_iso_image.cc", "ctool.cc",
                 "cupidbuild_host.cc", "path_encoding.cc")
        command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                   "-D_CRT_SECURE_NO_WARNINGS", "-I", str(ROOT / "toolchain"), "-x", "c",
                   *[str(ROOT / "toolchain" / name) for name in names],
                   *(["-lntdll"] if os.name == "nt" else []), "-o", str(cls.program)]
        result = subprocess.run(command, capture_output=True, timeout=180)
        if result.returncode:
            raise AssertionError(result.stdout.decode(errors="replace") + result.stderr.decode(errors="replace"))

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="iso-capture-fixture-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.manifest = "fixtures.manifest"
        self.fixtures = "fixtures"
        (self.root / self.fixtures).mkdir()

    def arrange(self, rows, manifest=None):
        for logical, payload in rows:
            path = self.root / self.fixtures / logical
            path.parent.mkdir(parents=True, exist_ok=True)
            if payload is None:
                path.mkdir(exist_ok=True)
            else:
                path.write_bytes(payload)
        if manifest is None:
            manifest = "\n".join(name for name, _ in rows).encode("ascii") + b"\n"
        (self.root / self.manifest).parent.mkdir(parents=True, exist_ok=True)
        (self.root / self.manifest).write_bytes(manifest)
        return manifest

    def command(self, mode="capture", image=None):
        arguments = [str(self.program), str(self.root).encode("utf-8").hex(),
                     self.manifest.encode("utf-8").hex(), self.fixtures.encode("utf-8").hex(), mode]
        if image is not None:
            arguments.append(image.encode("utf-8").hex())
        return arguments

    def run_case(self, mode="capture", image=None, success=True):
        result = subprocess.run(self.command(mode, image), capture_output=True, timeout=60)
        self.assertEqual(result.returncode, 0 if success else 1, result.stdout + result.stderr)
        return result

    def check(self, rows, manifest, mode="capture", image=None):
        result = self.run_case(mode, image)
        self.assertEqual(result.stderr, b"")
        lines = result.stdout.decode("ascii").splitlines()
        directories = [name for name, payload in rows if payload is None]
        expected = [len(rows), len(directories) + 1, len(rows) - len(directories),
                    max([1] + [name.count("/") + 2 for name in directories]),
                    sum(len(payload) for _, payload in rows if payload is not None), digest(manifest)]
        self.assertEqual(lines[0].split(), ["capture", *map(str, expected)])
        actual = {parts[1]: tuple(map(int, parts[2:])) for parts in (line.split() for line in lines[1:])}
        self.assertEqual(actual, {name: (1, 0, 0) if payload is None else (2, len(payload), digest(payload))
                                  for name, payload in rows})

    def reject(self, message=None):
        result = self.run_case("reject")
        self.assertEqual(result.stdout, b"")
        self.assertTrue(result.stderr.strip())
        if message:
            self.assertIn(message.encode(), result.stderr)

    def mutate(self, action):
        process = subprocess.Popen(self.command("wait"), stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        ready = queue.Queue()
        threading.Thread(target=lambda: ready.put(process.stdout.readline()), daemon=True).start()
        try:
            before = ready.get(timeout=30).strip().split()
            self.assertEqual(before[0], b"ready")
            expected = action()
            stdout, stderr = process.communicate(b"p" if expected else b"f", timeout=60)
            self.assertEqual((process.returncode, stdout.split(), stderr), (0, [b"retained", before[1]], b""))
        finally:
            if process.poll() is None:
                process.kill()
                process.communicate()

    def test_active_fixture_checked_author_and_independent_image(self):
        shutil.copytree(ROOT / "test_iso/fixtures", self.root / self.fixtures, dirs_exist_ok=True)
        (self.root / self.fixtures / "big.bin").write_bytes(bytes(range(256)) * 16)
        manifest = (ROOT / "test_iso/fixtures.manifest").read_bytes()
        (self.root / self.manifest).write_bytes(manifest)
        tree = self.root / self.fixtures
        rows = [(path.relative_to(tree).as_posix(), None if path.is_dir() else path.read_bytes())
                for path in sorted(tree.rglob("*"))]
        tool = ROOT / "bootstrap/seeds" / ("i386-windows/cupidobj.exe" if os.name == "nt" else "i386-linux/cupidobj.elf")
        arguments = [str(tool), "iso-fixture", str(self.root / self.manifest)]
        for name, payload in rows:
            arguments.extend(["--directory", name] if payload is None else
                             ["--file", name, str(tree / name)])
        image = self.root / "candidate.iso"
        result = subprocess.run([*arguments, "-o", str(image)], capture_output=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(image.read_bytes(), hostbuild._render_iso_image(hostbuild._snapshot_iso_tree(tree)))
        self.check(rows, manifest, "image", "candidate.iso")
        damaged = bytearray(image.read_bytes()); damaged[-1] ^= 1; image.write_bytes(damaged)
        self.run_case("image", "candidate.iso", success=False)

    def test_empty_files_directories_line_endings_order_and_borrowed_lifetime(self):
        rows = [("d", None), ("d/f", b"abc"), ("empty", b""), ("unused", None)]
        for manifest in (b"d\nd/f\nempty\nunused\n", b"unused\r\nempty\r\nd/f\r\nd",
                         b"d/f\nunused\nd\nempty"):
            with self.subTest(manifest=manifest):
                self.arrange(rows, manifest)
                self.check(rows, manifest, "repeat")

    def test_unicode_host_paths_and_read_only_capture(self):
        renamed = self.root.with_name(self.root.name + "-資料-😀")
        self.root.rename(renamed); self.addCleanup(shutil.rmtree, renamed); self.root = renamed
        self.fixtures = "資料-😀/tree"; self.manifest = "文書/café.manifest"
        rows = [("empty", None), ("payload", b"utf8")]
        manifest = self.arrange(rows)
        before = {p.relative_to(self.root).as_posix(): None if p.is_dir() else p.read_bytes()
                  for p in self.root.rglob("*")}
        self.check(rows, manifest)
        self.assertEqual(before, {p.relative_to(self.root).as_posix(): None if p.is_dir() else p.read_bytes()
                                 for p in self.root.rglob("*")})

    def test_full_flat_file_and_directory_boundaries(self):
        original = self.root
        for directories in (False, True):
            with self.subTest(directories=directories):
                self.root = original / str(directories); self.root.mkdir()
                (self.root / self.fixtures).mkdir()
                rows = [(f"f{index:03}", None if directories else b"x") for index in range(512)]
                self.check(rows, self.arrange(rows))

    def test_full_maximum_depth_boundary_and_component_length(self):
        directories = ["/".join(f"d{index}" for index in range(depth)) for depth in range(1, 8)]
        rows = [(name, None) for name in directories]
        rows.extend((directories[-1] + f"/f{index:03}", b"x") for index in range(505))
        self.check(rows, self.arrange(rows))
        other = self.root / "long"; other.mkdir(); self.root = other
        self.check([("a" * 127, b"long")], self.arrange([("a" * 127, b"long")]))

    def test_payload_block_boundaries_and_hardlinked_inputs(self):
        rows = [(f"f{size}", bytes(range(256)) * (size // 256) + bytes(range(size % 256)))
                for size in (0, 1, 2047, 2048, 2049, 4096, 4097)]
        self.check(rows, self.arrange(rows))
        os.link(self.root / self.fixtures / "f2048", self.root / self.fixtures / "alias")
        rows.append(("alias", dict(rows)["f2048"]))
        self.check(rows, self.arrange(rows))

    def test_null_arguments_bounded_diagnostics_and_zero_capacity(self):
        rows = [("f", b"one")]; manifest = self.arrange(rows)
        self.run_case("arguments")
        self.check(rows, manifest, "zero")

    def test_empty_fixture_path_selects_retained_root(self):
        self.fixtures = ""
        manifest = b"fixtures\nf\nfixtures.manifest\n"
        rows = [("fixtures", None), ("f", b"root"), ("fixtures.manifest", manifest)]
        self.check(rows, self.arrange(rows, manifest))

    def test_unsafe_manifest_spellings_and_entry_overflow(self):
        self.arrange([("f", b"one")])
        for manifest in (b"", b"\n", b"f\n\n", b"/f\n", b"../f\n", b"a/../f\n", b"f\\x\n",
                         b"a//f\n", b"f/\n", b"f\0other\n", b"f\rx\n", b"bad:name\n", b"caf\xc3\xa9\n",
                         b"a" * 128 + b"\n", b"f\n" * 513):
            with self.subTest(manifest=manifest):
                (self.root / self.manifest).write_bytes(manifest); self.reject()

    def test_duplicate_case_parent_kind_and_depth_rejection(self):
        rows = [("d", None), ("d/f", b"one"), ("f", b"two")]; self.arrange(rows)
        for manifest in (b"d\nd/f\nf\nf\n", b"d/f\nf\n", b"d\nD/f\nf\n", b"d\nd/f\nf/x\n"):
            with self.subTest(manifest=manifest):
                (self.root / self.manifest).write_bytes(manifest); self.reject()
        directories = ["/".join(["d"] * depth) for depth in range(1, 9)]
        self.arrange([(name, None) for name in directories]); self.reject("depth")

    def test_undeclared_and_missing_members_including_empty_directories(self):
        rows = [("d", None), ("d/f", b"one"), ("empty", None)]
        self.arrange(rows)
        for parent, name in (("", "hidden"), ("d", "extra"), ("empty", "unlisted")):
            with self.subTest(parent=parent):
                extra = self.root / self.fixtures / parent / name; extra.touch()
                self.reject(); extra.unlink()
        (self.root / self.fixtures / "d/f").unlink(); self.reject()

    def test_manifest_and_fixture_root_wrong_kinds_and_invalid_host_paths(self):
        self.arrange([("f", b"one")])
        original_manifest, original_fixtures = self.manifest, self.fixtures
        self.manifest = self.fixtures; self.reject()
        self.manifest = original_manifest; self.fixtures = original_manifest; self.reject("directory")
        self.fixtures = original_fixtures
        for manifest in ("../fixtures.manifest", "/fixtures.manifest", "fixtures.manifest/child"):
            with self.subTest(manifest=manifest):
                self.manifest = manifest; self.reject()
        self.manifest = original_manifest
        for fixtures in ("../fixtures", "/fixtures", "fixtures//child", "missing", "fixtures\\child"):
            with self.subTest(fixtures=fixtures):
                self.fixtures = fixtures; self.reject()

    def test_payload_and_manifest_restored_timestamp_drift_preserve_capture(self):
        self.arrange([("f", b"one"), ("g", b"two")])
        for logical, data in ((self.fixtures + "/f", b"six"), (self.manifest, b"g\nf\n")):
            with self.subTest(logical=logical):
                def change():
                    path = self.root / logical; before = path.stat()
                    path.write_bytes(data); os.utime(path, ns=(before.st_atime_ns, before.st_mtime_ns))
                    return False
                self.mutate(change)

    def test_exact_membership_drift_at_fixture_root_and_empty_directory(self):
        self.arrange([("f", b"one"), ("empty", None)])
        for directory in (self.fixtures, self.fixtures + "/empty"):
            with self.subTest(directory=directory):
                target = self.root / directory / "added"
                def change():
                    target.write_bytes(b"new"); return False
                self.mutate(change); target.unlink()

    def test_unobserved_repository_payload_does_not_change_fixture_inventory(self):
        self.arrange([("f", b"one")])
        (self.root / "unrelated").write_bytes(b"old")
        def change():
            (self.root / "unrelated").write_bytes(b"new"); return True
        self.mutate(change)

    def test_original_file_and_directory_bindings_remain_retained(self):
        original = self.root
        for logical in (self.fixtures + "/f", self.fixtures + "/empty", self.fixtures):
            with self.subTest(logical=logical):
                self.root = original / ("case-" + logical.replace("/", "-")); self.root.mkdir()
                self.arrange([("f", b"one"), ("empty", None)])
                def replace():
                    path = self.root / logical
                    try:
                        path.rename(path.with_name(path.name + "-old"))
                    except PermissionError:
                        self.assertEqual(os.name, "nt"); return True
                    if logical.endswith("/f"):
                        path.write_bytes(b"one")
                    else:
                        path.mkdir()
                        if logical == self.fixtures:
                            (path / "f").write_bytes(b"one"); (path / "empty").mkdir()
                    return False
                self.mutate(replace)

    def test_oversized_payload_fails_before_copying_file_bytes(self):
        self.arrange([("large", b"")])
        with (self.root / self.fixtures / "large").open("r+b") as stream:
            stream.truncate(64 * 1024 * 1024 + 1)
        self.reject("payload storage")

    @unittest.skipIf(os.name == "nt", "POSIX links and FIFO")
    def test_links_special_files_and_linked_ancestors_fail_without_blocking(self):
        self.arrange([("f", b"one")])
        tree = self.root / self.fixtures
        (tree / "link").symlink_to(tree / "f"); os.mkfifo(tree / "fifo")
        (tree / "directory").symlink_to(tree, target_is_directory=True)
        for name in ("link", "fifo", "directory", "directory/f"):
            with self.subTest(name=name):
                (self.root / self.manifest).write_text(name + "\n", encoding="ascii"); self.reject()

    @unittest.skipUnless(os.name == "nt", "Windows directory junction")
    def test_junction_and_linked_fixture_root_fail(self):
        self.arrange([("f", b"one")])
        link = self.root / "junction"
        result = subprocess.run(["cmd", "/c", "mklink", "/J", str(link), str(self.root / self.fixtures)],
                                capture_output=True, timeout=15)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.fixtures = "junction"; self.reject()

    def test_independent_concurrent_captures(self):
        rows = [("d", None), ("d/f", b"one"), ("empty", b"")]
        manifest = self.arrange(rows)
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            results = list(pool.map(lambda _: subprocess.run(self.command(), capture_output=True, timeout=60), range(12)))
        for result in results:
            self.assertEqual((result.returncode, result.stderr), (0, b""))
        self.assertEqual(len({result.stdout for result in results}), 1)
        self.check(rows, manifest)


if __name__ == "__main__":
    unittest.main()

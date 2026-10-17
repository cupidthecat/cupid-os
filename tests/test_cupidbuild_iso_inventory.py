"""Native ISO inventory checks against the active fixture and existing author."""
import concurrent.futures
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tools import hostbuild


ROOT = Path(__file__).resolve().parents[1]


class String(ctypes.Structure):
    _fields_ = [("data", ctypes.c_char_p), ("size", ctypes.c_uint)]


class Bytes(ctypes.Structure):
    _fields_ = [("data", ctypes.c_void_p), ("size", ctypes.c_uint)]


class Source(ctypes.Structure):
    _fields_ = [("path", String), ("contents", Bytes)]


class Entry(ctypes.Structure):
    _fields_ = [("path", String), ("kind", ctypes.c_int), ("source", ctypes.POINTER(Source))]


class Inventory(ctypes.Structure):
    _fields_ = [("entries", ctypes.POINTER(Entry)), ("entry_count", ctypes.c_uint)]


class Report(ctypes.Structure):
    _fields_ = [("directories", ctypes.c_uint), ("files", ctypes.c_uint),
                ("directory_depth", ctypes.c_uint), ("file_bytes", ctypes.c_ulonglong)]


class CupidBuildIsoInventoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="native-iso-inventory-")
        cls.addClassCleanup(cls.build.cleanup)
        compiler = shutil.which("clang" if os.name == "nt" else "cc")
        if compiler is None:
            raise AssertionError("Native contract compiler is unavailable")
        target = Path(cls.build.name) / ("iso.dll" if os.name == "nt" else "iso.so")
        flags = (["-Wl,/export:cupidbuild_iso_inventory_validate"]
                 if os.name == "nt" else ["-fPIC"])
        result = subprocess.run([
            compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-shared",
            "-x", "c", str(ROOT / "toolchain/cupidbuild_iso.cc"), *flags,
            "-o", str(target),
        ], capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        cls.library = ctypes.CDLL(str(target))
        if os.name == "nt":
            cls.addClassCleanup(cls.unload_library)
        cls.validate = cls.library.cupidbuild_iso_inventory_validate
        cls.validate.argtypes = [ctypes.POINTER(Source), ctypes.POINTER(Inventory),
                                 ctypes.POINTER(Report), ctypes.c_void_p, ctypes.c_uint]
        cls.validate.restype = ctypes.c_int

    @classmethod
    def unload_library(cls):
        import _ctypes
        _ctypes.FreeLibrary(cls.library._handle)
        cls.library._handle = 0

    def request(self, rows, manifest=None):
        # Keep every borrowed view alive for the whole native call.
        owned = []
        entries = (Entry * len(rows))()
        for index, (path, payload) in enumerate(rows):
            spelling = path.encode("ascii") if isinstance(path, str) else path
            owned.append(spelling)
            entries[index].path = String(spelling, len(spelling))
            entries[index].kind = 1 if payload is None else 2
            if payload is not None:
                buffer = ctypes.create_string_buffer(payload)
                source = Source(String(spelling, len(spelling)), Bytes(ctypes.addressof(buffer), len(payload)))
                owned.extend((buffer, source))
                entries[index].source = ctypes.pointer(source)
        if manifest is None:
            manifest = b"\n".join(entry.path.data for entry in entries) + b"\n"
        buffer = ctypes.create_string_buffer(manifest)
        source = Source(String(b"fixtures.manifest", 17), Bytes(ctypes.addressof(buffer), len(manifest)))
        inventory = Inventory(entries, len(entries))
        owned.extend((buffer, source, inventory, entries))
        return source, inventory, owned

    def call(self, source, inventory, capacity=512):
        report = Report(99, 99, 99, 99)
        storage = ctypes.create_string_buffer(b"?" * (capacity + 8))
        result = self.validate(ctypes.byref(source), ctypes.byref(inventory), ctypes.byref(report),
                               storage if capacity else None, capacity)
        self.assertEqual(storage.raw[capacity:capacity + 8], b"?" * 8)
        if not result:
            self.assertEqual((report.directories, report.files, report.directory_depth, report.file_bytes),
                             (0, 0, 0, 0))
        return result, report, storage.value.decode("ascii") if capacity else ""

    def check(self, rows, manifest=None, message=None):
        source, inventory, owned = self.request(rows, manifest)
        result, report, error = self.call(source, inventory)
        if message is None:
            self.assertEqual(result, 1, error)
            self.assertEqual(error, "")
        else:
            self.assertEqual(result, 0)
            self.assertIn(message, error)
        return report

    def test_active_fixture_matches_existing_author_and_independent_snapshot(self):
        with tempfile.TemporaryDirectory(prefix="iso-active-tree-") as directory:
            root = Path(directory)
            fixtures = root / "fixtures"
            shutil.copytree(ROOT / "test_iso/fixtures", fixtures)
            (fixtures / "big.bin").write_bytes(bytes(range(256)) * 16)
            manifest = root / "fixtures.manifest"
            manifest.write_bytes((ROOT / "test_iso/fixtures.manifest").read_bytes())
            snapshot = hostbuild._snapshot_iso_tree(fixtures)
            captured = hostbuild._read_iso_manifest(manifest, fixtures)
            hostbuild._validate_iso_manifest(snapshot, captured)
            rows = []
            command = [str(ROOT / "bootstrap/seeds" /
                           ("i386-windows/cupidobj.exe" if os.name == "nt" else "i386-linux/cupidobj.elf")),
                       "iso-fixture", str(manifest)]
            for path in sorted(fixtures.rglob("*")):
                logical = path.relative_to(fixtures).as_posix()
                payload = None if path.is_dir() else path.read_bytes()
                rows.append((logical, payload))
                command.extend(["--directory", logical] if payload is None else ["--file", logical, str(path)])
            output = root / "checked.iso"
            result = subprocess.run([*command, "-o", str(output)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(output.read_bytes(), hostbuild._render_iso_image(snapshot))
            report = self.check(rows, manifest.read_bytes())
            self.assertEqual((report.directories, report.files, report.directory_depth), (2, 6, 2))
            self.assertEqual(report.file_bytes, sum(len(payload) for _, payload in rows if payload is not None))

    def test_entry_and_manifest_order_crlf_and_missing_final_newline(self):
        rows = [("d", None), ("d/f.txt", b"nested"), ("empty", b""), ("unused", None)]
        for ordered in (rows, list(reversed(rows))):
            for manifest in (b"d\nd/f.txt\nempty\nunused\n", b"unused\r\nempty\r\nd/f.txt\r\nd",
                             b"empty\nd/f.txt\nunused\nd"):
                with self.subTest(order=ordered, manifest=manifest):
                    report = self.check(ordered, manifest)
                    self.assertEqual((report.directories, report.files, report.directory_depth, report.file_bytes),
                                     (3, 2, 2, 6))

    def test_full_512_entry_boundary_in_both_orders(self):
        rows = [(f"d{index:03}", None) for index in range(512)]
        manifest = b"\n".join(name.encode() for name, _ in rows) + b"\n"
        for ordered in (rows, list(reversed(rows))):
            report = self.check(ordered, manifest)
            self.assertEqual((report.directories, report.files), (513, 0))
        self.check([*rows, ("overflow", None)], message="512")

    def test_eight_directory_levels_include_implicit_root(self):
        rows = [("/".join(["d"] * depth), None) for depth in range(1, 8)]
        rows.append(("/".join(["d"] * 7 + ["f"]), b"x"))
        self.assertEqual(self.check(rows).directory_depth, 8)
        self.check([*rows[:-1], ("/".join(["d"] * 8), None)], message="depth")
        self.check([*rows, ("/".join(["d"] * 8 + ["f"]), b"x")], message="depth")

    def test_portable_component_boundary_and_invalid_paths(self):
        self.check([("a" * 127, b"")])
        for path in (b"", b"/a", b"a/", b"a//b", b".", b"..", b"a/../b", b"a\\b",
                     b"a b", b"a\tb", b"a\x00b", b"a\xffb", b"a:1", b"a" * 128, b"a" * 1024):
            with self.subTest(path=path):
                self.check([(path, b"")], message="path")

    def test_requires_every_exact_directory_parent(self):
        for rows in ([("d/f", b"x")], [("d", b""), ("d/f", b"x")],
                     [("D", None), ("d/f", b"x")]):
            with self.subTest(rows=rows):
                self.check(rows, message="parent")

    def test_inventory_duplicate_and_case_collision(self):
        for rows in ([("a", b""), ("a", b"x")], [("A", None), ("a", b"")]):
            self.check(rows, message="collision")

    def test_manifest_missing_substituted_duplicate_and_case_drift(self):
        rows = [("a", b""), ("b", b"x")]
        for manifest, message in ((b"a\n", "absent"), (b"a\nc\n", "no captured"),
                                  (b"a\na\nb\n", "duplicate"), (b"A\nb\n", "case collision")):
            with self.subTest(manifest=manifest):
                self.check(rows, manifest, message)
        self.check(rows)

    def test_manifest_blank_non_ascii_and_unsafe_forms(self):
        for manifest in (b"", b"\n", b"a\n\n", b" a\n", b"a \n", b"a\rb\n", b"a\x00\n",
                         b"\xff\n", b"/a\n", b"./a\n", b"a//b\n", b"a\\b\n", b"a\t\n"):
            with self.subTest(manifest=manifest):
                self.check([("a", b"")], manifest, "manifest")

    def test_source_kind_and_payload_view_rejections_and_recovery(self):
        source, inventory, owned = self.request([("a", b"x")])
        entry = inventory.entries[0]
        entry.kind = 9
        self.assertIn("kind", self.call(source, inventory)[2])
        entry.kind = 1
        self.assertIn("kind", self.call(source, inventory)[2])
        entry.kind = 2
        payload = entry.source.contents
        entry.source = ctypes.POINTER(Source)()
        self.assertIn("kind", self.call(source, inventory)[2])
        entry.source = ctypes.pointer(payload)
        original = entry.source.contents.contents.data
        entry.source.contents.contents.data = None
        self.assertIn("payload", self.call(source, inventory)[2])
        entry.source.contents.contents.data = original
        self.assertEqual(self.call(source, inventory)[0], 1)

    def test_empty_file_permits_null_payload(self):
        source, inventory, owned = self.request([("empty", b"")])
        inventory.entries[0].source.contents.contents.data = None
        report = self.call(source, inventory)[1]
        self.assertEqual((report.files, report.file_bytes), (1, 0))

    def test_invalid_request_views_clear_results(self):
        source, inventory, owned = self.request([("a", b"")])
        report = Report(99, 99, 99, 99)
        error = ctypes.create_string_buffer(256)
        for manifest, entries, result in ((None, ctypes.byref(inventory), ctypes.byref(report)),
                                          (ctypes.byref(source), None, ctypes.byref(report)),
                                          (ctypes.byref(source), ctypes.byref(inventory), None)):
            self.assertEqual(self.validate(manifest, entries, result, error, len(error)), 0)
        inventory.entry_count = 0
        self.assertEqual(self.call(source, inventory)[0], 0)
        inventory.entry_count = 1
        inventory.entries = ctypes.POINTER(Entry)()
        self.assertEqual(self.call(source, inventory)[0], 0)
        source.contents.data = None
        self.assertEqual(self.call(source, inventory)[0], 0)

    def test_bounded_diagnostics_and_null_diagnostic_contract(self):
        source, inventory, owned = self.request([("a", b"")], b"b\n")
        for capacity in (0, 1, 2, 17, 128):
            result, report, error = self.call(source, inventory, capacity)
            self.assertEqual(result, 0)
            if capacity:
                self.assertLess(len(error), capacity)
        report = Report(99, 99, 99, 99)
        self.assertEqual(self.validate(ctypes.byref(source), ctypes.byref(inventory), ctypes.byref(report), None, 1), 0)
        self.assertEqual(report.files, 0)
        source, inventory, owned = self.request([("a", b"")])
        self.assertEqual(self.call(source, inventory, 0)[0], 1)

    def test_reentrant_concurrent_calls_do_not_share_state(self):
        def run(index):
            source, inventory, owned = self.request([(f"f{index}", bytes([index]) * index)])
            result, report, error = self.call(source, inventory)
            return result, report.file_bytes, error
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
            self.assertEqual(list(executor.map(run, range(64))), [(1, index, "") for index in range(64)])


if __name__ == "__main__":
    unittest.main()

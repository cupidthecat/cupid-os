"""Independent byte checks for captured deterministic ISO fixtures."""
import concurrent.futures
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tools import hostbuild
from tests.test_cupidbuild_iso_inventory import Bytes, Entry, Inventory, Report, Source, String


ROOT = Path(__file__).resolve().parents[1]


class ImageReport(ctypes.Structure):
    _fields_ = [("inventory", Report), ("image_bytes", ctypes.c_uint),
                ("blocks", ctypes.c_uint), ("path_table_bytes", ctypes.c_uint),
                ("continuation_block", ctypes.c_uint)]


class Mark(ctypes.Structure):
    _fields_ = [("owner", ctypes.c_void_p), ("block", ctypes.c_void_p),
                ("used", ctypes.c_uint), ("generation", ctypes.c_uint)]


def report_values(report):
    return (report.inventory.directories, report.inventory.files,
            report.inventory.directory_depth, report.inventory.file_bytes,
            report.image_bytes, report.blocks, report.path_table_bytes,
            report.continuation_block)


class CupidBuildIsoImageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="native-iso-image-")
        cls.addClassCleanup(cls.build.cleanup)
        compiler = shutil.which("clang" if os.name == "nt" else "cc")
        if compiler is None:
            raise AssertionError("Native contract compiler is unavailable")
        target = Path(cls.build.name) / ("iso.dll" if os.name == "nt" else "iso.so")
        exports = ("cupidbuild_iso_image_validate", "iso_image_test_arena_open",
                   "iso_image_test_arena_close", "ctool_arena_alloc",
                   "ctool_arena_mark", "ctool_arena_rewind")
        flags = ([f"-Wl,/export:{name}" for name in exports]
                 if os.name == "nt" else ["-fPIC"])
        if os.name == "nt":
            flags.append("-D_CRT_SECURE_NO_WARNINGS")
        result = subprocess.run([
            compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-shared",
            "-x", "c", "-I", str(ROOT / "toolchain"),
            *[str(ROOT / "toolchain" / name) for name in
              ("ctool.cc", "cupidbuild_iso.cc", "cupidbuild_iso_image.cc",
               "tests/cupidbuild_iso_image_contract.cc")], *flags, "-o", str(target),
        ], capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        cls.library = ctypes.CDLL(str(target))
        if os.name == "nt":
            cls.addClassCleanup(cls.unload_library)
        cls.validate = cls.library.cupidbuild_iso_image_validate
        cls.validate.argtypes = [ctypes.c_void_p, ctypes.POINTER(Source),
                                 ctypes.POINTER(Inventory), ctypes.POINTER(Source),
                                 ctypes.POINTER(ImageReport), ctypes.c_void_p, ctypes.c_uint]
        cls.validate.restype = ctypes.c_int
        cls.open_arena = cls.library.iso_image_test_arena_open
        cls.open_arena.argtypes = [ctypes.c_uint]
        cls.open_arena.restype = ctypes.c_void_p
        cls.close_arena = cls.library.iso_image_test_arena_close
        cls.close_arena.argtypes = [ctypes.c_void_p]
        cls.close_arena.restype = None
        cls.allocate = cls.library.ctool_arena_alloc
        cls.allocate.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint,
                                  ctypes.POINTER(ctypes.c_void_p)]
        cls.allocate.restype = ctypes.c_int
        cls.mark = cls.library.ctool_arena_mark
        cls.mark.argtypes = [ctypes.c_void_p]
        cls.mark.restype = Mark
        cls.program = os.environ.get("CUPIDBUILD_ISO_IMAGE_PROGRAM")

    @classmethod
    def unload_library(cls):
        import _ctypes
        _ctypes.FreeLibrary(cls.library._handle)
        cls.library._handle = 0

    def request(self, rows, manifest=None):
        owned = []
        entries = (Entry * len(rows))()
        for index, (path, payload) in enumerate(rows):
            spelling = path.encode("ascii")
            owned.append(spelling)
            entries[index].path = String(spelling, len(spelling))
            entries[index].kind = 1 if payload is None else 2
            if payload is not None:
                buffer = ctypes.create_string_buffer(payload)
                source = Source(String(spelling, len(spelling)),
                                Bytes(ctypes.addressof(buffer), len(payload)))
                owned.extend((buffer, source))
                entries[index].source = ctypes.pointer(source)
        if manifest is None:
            manifest = "\n".join(path for path, _ in rows).encode("ascii") + b"\n"
        buffer = ctypes.create_string_buffer(manifest)
        source = Source(String(b"fixtures.manifest", 17),
                        Bytes(ctypes.addressof(buffer), len(manifest)))
        inventory = Inventory(entries, len(entries))
        owned.extend((buffer, entries, inventory, source))
        inventory.rows = rows
        source.manifest_bytes = manifest
        return source, inventory, owned

    def render(self, rows):
        # The checker consumes logical names, including trailing dots that the
        # ordinary Windows filesystem spelling would normalize.
        def snapshot(path, payload):
            children = tuple(snapshot(name, data) for name, data in
                             sorted(rows, key=lambda row: (row[0].lower(), row[0]))
                             if name.rpartition("/")[0] == path)
            return hostbuild._IsoSource(path.rpartition("/")[2], path, payload, children)
        return hostbuild._render_iso_image(snapshot("", None))

    def checked_call(self, manifest, inventory, image, capacity, limit):
        with tempfile.TemporaryDirectory(prefix="iso-image-checked-") as directory:
            root = Path(directory)
            (root / "manifest").write_bytes(manifest.manifest_bytes)
            (root / "image").write_bytes(image)
            command = [self.program, "manifest", "image", str(capacity), str(limit)]
            for index, (path, payload) in enumerate(inventory.rows):
                if payload is None:
                    command.extend(("--directory", path))
                else:
                    name = f"f{index:03}"
                    (root / name).write_bytes(payload)
                    command.extend(("--file", path, name))
            result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=60)
            self.assertIn(result.returncode, (0, 1), result.stdout + result.stderr)
            lines = result.stdout.splitlines()
            self.assertGreaterEqual(len(lines), 1, result.stdout + result.stderr)
            values = tuple(int(value) for value in lines[0].split())
            error = lines[1] if len(lines) > 1 else ""
            return 1 - result.returncode, values, error

    def call(self, manifest, inventory, image, capacity=512, arena=None, limit=131072,
             checked=True):
        own_arena = arena is None
        if own_arena:
            arena = self.open_arena(limit)
            self.assertTrue(arena)
        buffer = ctypes.create_string_buffer(image)
        source = Source(String(b"candidate.iso", 13), Bytes(ctypes.addressof(buffer), len(image)))
        report = ImageReport()
        ctypes.memset(ctypes.byref(report), 0x99, ctypes.sizeof(report))
        error = ctypes.create_string_buffer(b"?" * (capacity + 8))
        try:
            result = self.validate(arena, ctypes.byref(manifest), ctypes.byref(inventory),
                                   ctypes.byref(source), ctypes.byref(report),
                                   error if capacity else None, capacity)
        finally:
            if own_arena:
                self.close_arena(arena)
        self.assertEqual(error.raw[capacity:capacity + 8], b"?" * 8)
        diagnostic = error.value.decode("ascii") if capacity else ""
        if not result:
            self.assertEqual(report_values(report), (0,) * 8)
        if checked and self.program:
            actual = self.checked_call(manifest, inventory, image, capacity, limit)
            self.assertEqual(actual, (result, report_values(report), diagnostic))
        return result, report, diagnostic

    def check(self, rows, image=None, manifest=None, message=None, **options):
        source, inventory, owned = self.request(rows, manifest)
        if image is None:
            image = self.render(rows)
        result, report, error = self.call(source, inventory, image, **options)
        self.assertEqual(result, 1 if message is None else 0, error)
        if message is None:
            self.assertEqual(error, "")
            self.assertEqual(report.image_bytes, len(image))
            self.assertEqual(report.blocks * 2048, len(image))
            self.assertEqual(report.inventory.file_bytes,
                             sum(len(payload) for _, payload in rows if payload is not None))
        else:
            self.assertIn(message, error)
        return report

    def test_active_fixture_matches_checked_author(self):
        with tempfile.TemporaryDirectory(prefix="iso-image-active-") as directory:
            root = Path(directory)
            shutil.copytree(ROOT / "test_iso/fixtures", root / "fixtures")
            (root / "fixtures/big.bin").write_bytes(bytes(range(256)) * 16)
            manifest = root / "manifest"
            manifest.write_bytes((ROOT / "test_iso/fixtures.manifest").read_bytes())
            rows = []
            command = [str(ROOT / "bootstrap/seeds" /
                           ("i386-windows/cupidobj.exe" if os.name == "nt" else "i386-linux/cupidobj.elf")),
                       "iso-fixture", str(manifest)]
            for path in sorted((root / "fixtures").rglob("*")):
                name = path.relative_to(root / "fixtures").as_posix()
                payload = None if path.is_dir() else path.read_bytes()
                rows.append((name, payload))
                command.extend(["--directory", name] if payload is None else ["--file", name, str(path)])
            output = root / "checked.iso"
            result = subprocess.run([*command, "-o", str(output)], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            image = output.read_bytes()
            self.assertEqual(image, self.render(rows))
            report = self.check(rows, image, manifest.read_bytes())
            self.assertEqual((report.inventory.directories, report.inventory.files), (2, 6))

    def test_order_manifest_line_endings_and_empty_members(self):
        rows = [("d", None), ("d/f.txt", b"nested"), ("empty", b""), ("unused", None)]
        image = self.render(rows)
        for ordered in (rows, list(reversed(rows))):
            for manifest in (b"d\nd/f.txt\nempty\nunused\n", b"unused\r\nempty\r\nd/f.txt\r\nd",
                             b"empty\nd/f.txt\nunused\nd"):
                self.check(ordered, image, manifest)

    def test_identifier_collisions_punctuation_extensions_and_breadth_first_order(self):
        rows = [("abc", None), ("abc/z", None), ("abc/z/f", b"nested"), ("Z_", None),
                ("abcdefgh1.txt", b"1"), ("abcdefgh2.txt", b"2"), ("ABCD_1.TXT", b"3"),
                (".hidden", b"4"), ("trailing.", b"5"), ("noextension", b"6"),
                ("a-b.c-d", b"7"), ("a_b.c_d", b"8"), ("a.b.c-d", b"9")]
        image = self.render(rows)
        with tempfile.TemporaryDirectory(prefix="iso-image-identifiers-") as directory:
            root = Path(directory)
            (root / "manifest").write_bytes("\n".join(path for path, _ in rows).encode() + b"\n")
            command = [str(ROOT / "bootstrap/seeds" /
                           ("i386-windows/cupidobj.exe" if os.name == "nt" else "i386-linux/cupidobj.elf")),
                       "iso-fixture", "manifest"]
            for index, (path, payload) in enumerate(rows):
                if payload is None:
                    command.extend(("--directory", path))
                else:
                    name = f"f{index:03}"
                    (root / name).write_bytes(payload)
                    command.extend(("--file", path, name))
            result = subprocess.run([*command, "-o", "checked.iso"], cwd=root,
                                    capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual((root / "checked.iso").read_bytes(), image)
        self.check(list(reversed(rows)), image)

    def test_exact_and_spanning_payload_blocks(self):
        for size in (0, 1, 2047, 2048, 2049, 4096, 4097):
            with self.subTest(size=size):
                self.check([("payload.bin", (bytes(range(256)) * 17)[:size])])

    def test_full_directory_boundary_spans_both_path_tables(self):
        rows = [(f"d{index:03}", None) for index in range(512)]
        image = self.render(rows)
        for ordered in (rows, list(reversed(rows))):
            report = self.check(ordered, image)
            self.assertEqual(report.inventory.directories, 513)
            self.assertGreater(report.path_table_bytes, 2048)
        self.check([*rows, ("overflow", None)], image, message="512")

    def test_full_file_boundary_allocates_collision_suffixes(self):
        rows = [(f"abcdefgh-{index:03}.bin", b"x") for index in range(512)]
        self.check(list(reversed(rows)), self.render(rows))

    def test_maximum_directory_depth_and_component(self):
        rows = [("/".join([f"d{n}" for n in range(index + 1)]), None) for index in range(7)]
        parent = rows[-1][0]
        rows.extend((f"{parent}/f{index:03}.bin", b"x") for index in range(505))
        report = self.check(list(reversed(rows)), self.render(rows))
        self.assertEqual(report.inventory.directory_depth, 8)
        self.check([("n" * 127, b"long name"), ("d" * 127, None)])

    def test_every_system_descriptor_region_and_terminator_is_checked(self):
        rows = [("file.bin", b"payload")]
        image = self.render(rows)
        offsets = [0, 1, 32767, *range(32768, 34816, 97),
                   32768 + 80, 32768 + 84, 32768 + 128, 32768 + 132,
                   32768 + 140, 32768 + 148, 32768 + 156, 32768 + 813,
                   34816, 34822, 36863]
        for offset in offsets:
            changed = bytearray(image)
            changed[offset] ^= 1
            with self.subTest(offset=offset):
                self.check(rows, bytes(changed), message="differs")

    def test_both_path_tables_and_their_padding_are_checked(self):
        rows = [("d", None), ("d/f", b"data")]
        image = self.render(rows)
        for offset in [*range(18 * 2048, 18 * 2048 + 20), 19 * 2048 - 1,
                       *range(19 * 2048, 19 * 2048 + 20), 20 * 2048 - 1]:
            changed = bytearray(image)
            changed[offset] ^= 1
            with self.subTest(offset=offset):
                self.check(rows, bytes(changed), message="path tables")

    def test_directory_rock_ridge_and_continuation_bytes_are_checked(self):
        rows = [("d", None), ("d/f", b"payload"), ("empty", b"")]
        image = self.render(rows)
        report = self.check(rows, image)
        root_block = int.from_bytes(image[32768 + 158:32768 + 162], "little")
        offsets = [*range(root_block * 2048, root_block * 2048 + 380, 7),
                   report.continuation_block * 2048 - 1,
                   *range(report.continuation_block * 2048,
                          report.continuation_block * 2048 + 254, 7),
                   (report.continuation_block + 1) * 2048 - 1]
        for offset in offsets:
            changed = bytearray(image)
            changed[offset] ^= 1
            with self.subTest(offset=offset):
                self.check(rows, bytes(changed), message="differ")

    def test_payload_padding_and_captured_payload_changes_are_rejected(self):
        rows = [("a", b"payload"), ("b", b"second")]
        image = self.render(rows)
        for offset in range(len(image) - 4096, len(image), 131):
            changed = bytearray(image)
            changed[offset] ^= 1
            with self.subTest(offset=offset):
                self.check(rows, bytes(changed), message="payload")
        self.check([("a", b"PAYLOAD"), ("b", b"second")], image, message="payload")

    def test_truncated_trailing_and_relocated_images_are_rejected(self):
        rows = [("file", b"payload")]
        image = self.render(rows)
        for changed in (b"", image[:-1], image[:-2048], image + b"\x00", image + bytes(2048)):
            self.check(rows, changed, message="size")
        changed = bytearray(image)
        changed[-4096:-2048], changed[-2048:] = changed[-2048:], changed[-4096:-2048]
        self.check(rows, bytes(changed), message="continuation")

    def test_inventory_mismatch_and_allocation_failure_clear_report(self):
        rows = [("file", b"payload")]
        image = self.render(rows)
        self.check(rows, image, b"missing\n", message="manifest")
        self.check(rows, image, limit=4096, message="arena")

    def test_arena_storage_is_restored_after_success_and_failure(self):
        rows = [("file", b"payload")]
        image = self.render(rows)
        manifest, inventory, owned = self.request(rows)
        arena = self.open_arena(16384)
        self.assertTrue(arena)
        try:
            prefix = ctypes.c_void_p()
            self.assertEqual(self.allocate(arena, 37, 1, ctypes.byref(prefix)), 0)
            ctypes.memset(prefix, 0x5A, 37)
            before = self.mark(arena)
            for index in range(80):
                candidate = image if index % 2 == 0 else image[:-1]
                result, report, error = self.call(manifest, inventory, candidate,
                                                  arena=arena, limit=16384, checked=False)
                self.assertEqual(result, 1 if index % 2 == 0 else 0, error)
                after = self.mark(arena)
                self.assertEqual((after.owner, after.block, after.used, after.generation),
                                 (before.owner, before.block, before.used, before.generation))
                self.assertEqual(ctypes.string_at(prefix, 37), b"Z" * 37)
        finally:
            self.close_arena(arena)

    def test_null_arguments_and_bounded_diagnostics(self):
        rows = [("file", b"payload")]
        image = self.render(rows)
        for capacity in (0, 1, 2, 17):
            manifest, inventory, owned = self.request(rows)
            result, report, error = self.call(manifest, inventory, image[:-1], capacity)
            self.assertEqual(result, 0)
            self.assertLessEqual(len(error), max(0, capacity - 1))
        arena = self.open_arena(16384)
        manifest, inventory, owned = self.request(rows)
        buffer = ctypes.create_string_buffer(image)
        source = Source(String(b"image", 5), Bytes(ctypes.addressof(buffer), len(image)))
        report = ImageReport()
        error = ctypes.create_string_buffer(64)
        args = [arena, ctypes.byref(manifest), ctypes.byref(inventory), ctypes.byref(source),
                ctypes.byref(report), error, 64]
        try:
            for index in range(6):
                with self.subTest(null_argument=index):
                    changed = list(args)
                    changed[index] = None
                    self.assertEqual(self.validate(*changed), 0)
                    if index != 4:
                        self.assertEqual(report_values(report), (0,) * 8)
        finally:
            self.close_arena(arena)

    def test_layout_overflow_precedes_payload_reads(self):
        manifest, inventory, owned = self.request([("huge", b"x")])
        inventory.entries[0].source.contents.contents.size = 0xFFFFFFFF
        result, report, error = self.call(manifest, inventory, b"x", checked=False)
        self.assertEqual(result, 0)
        self.assertIn("32-bit", error)

    def test_independent_jobs_can_validate_concurrently(self):
        rows = [("d", None), ("d/f", bytes(range(256)) * 9)]
        image = self.render(rows)
        def validate(index):
            manifest, inventory, owned = self.request(rows)
            result, report, error = self.call(manifest, inventory,
                image if index % 2 == 0 else image[:-1], checked=False)
            return result, report_values(report), error
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
            results = list(executor.map(validate, range(64)))
        for index, (result, values, error) in enumerate(results):
            self.assertEqual(result, 1 if index % 2 == 0 else 0, error)
            self.assertEqual(values[:4], (2, 1, 2, 2304) if result else (0, 0, 0, 0))


if __name__ == "__main__":
    unittest.main()

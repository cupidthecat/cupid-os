"""Framing and real producer checks for complete ISO request transport."""
import concurrent.futures
import ctypes
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

from tests.test_cupidbuild_iso_inventory import Bytes, Entry, Inventory, Source, String
from tests.test_cupidbuild_iso_image import Mark
from tests.test_toolchain_cupidobj import _build_cli
from tools import hostbuild


ROOT = Path(__file__).resolve().parents[1]
MAGIC = b"CUPISO1\0"
MAX_MANIFEST = 1025 * 512


class Decoded(ctypes.Structure):
    _fields_ = [("manifest", Source), ("inventory", Inventory)]


def string_bytes(view):
    pointer = ctypes.cast(ctypes.byref(view), ctypes.POINTER(ctypes.c_void_p)).contents.value
    return ctypes.string_at(pointer, view.size)


def wire(rows, manifest=None):
    if manifest is None:
        manifest = b"\n".join(name for name, _ in rows) + b"\n"
    result = bytearray(MAGIC + struct.pack("<II", len(manifest), len(rows)) + manifest)
    for name, payload in rows:
        result.extend(struct.pack("<IIII", 1 if payload is None else 2,
                                  len(name), 0 if payload is None else len(payload), 0))
        result.extend(name)
        if payload is not None:
            result.extend(payload)
    return bytes(result)


def digest(rows, manifest):
    value = 2166136261
    for part in [manifest, *[part for name, payload in rows for part in
                            (bytes([1 if payload is None else 2]), name,
                             b"" if payload is None else payload)]]:
        for byte in part:
            value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


def image(rows):
    # This oracle operates on logical names, independent of filesystem spelling.
    names = [(name.decode("ascii"), payload) for name, payload in rows]
    def node(path, payload):
        children = tuple(node(name, data) for name, data in
                         sorted(names, key=lambda row: (row[0].lower(), row[0]))
                         if name.rpartition("/")[0] == path)
        return hostbuild._IsoSource(path.rpartition("/")[2], path, payload, children)
    return hostbuild._render_iso_image(node("", None))


class IsoFixtureBundleCodecTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="iso-bundle-codec-")
        cls.addClassCleanup(cls.build.cleanup)
        compiler = shutil.which("clang" if os.name == "nt" else "cc")
        if compiler is None:
            raise AssertionError("Native contract compiler is unavailable")
        target = Path(cls.build.name) / ("bundle.dll" if os.name == "nt" else "bundle.so")
        exports = ("ctool_iso_fixture_bundle_encode", "ctool_iso_fixture_bundle_decode",
                   "iso_bundle_test_arena_open", "iso_bundle_test_arena_close",
                   "iso_bundle_test_api", "ctool_arena_alloc", "ctool_arena_mark",
                   "ctool_arena_rewind")
        flags = ([f"-Wl,/export:{name}" for name in exports]
                 if os.name == "nt" else ["-fPIC"])
        if os.name == "nt":
            flags.append("-D_CRT_SECURE_NO_WARNINGS")
        result = subprocess.run([
            compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-shared",
            "-x", "c", "-I", str(ROOT / "toolchain"),
            *[str(ROOT / "toolchain" / name) for name in
              ("ctool.cc", "iso_fixture_bundle.cc", "tests/iso_fixture_bundle_contract.cc")],
            *flags, "-o", str(target),
        ], capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        cls.library = ctypes.CDLL(str(target))
        if os.name == "nt":
            cls.addClassCleanup(cls.unload_library)
        cls.decode = cls.library.ctool_iso_fixture_bundle_decode
        cls.decode.argtypes = [ctypes.c_void_p, ctypes.POINTER(Source),
                               ctypes.POINTER(Decoded), ctypes.c_void_p, ctypes.c_uint]
        cls.decode.restype = ctypes.c_int
        cls.encode = cls.library.ctool_iso_fixture_bundle_encode
        cls.encode.argtypes = [ctypes.c_void_p, ctypes.POINTER(Source),
                               ctypes.POINTER(Inventory), ctypes.POINTER(Bytes),
                               ctypes.c_void_p, ctypes.c_uint]
        cls.encode.restype = ctypes.c_int
        cls.open_arena = cls.library.iso_bundle_test_arena_open
        cls.open_arena.argtypes = [ctypes.c_uint]
        cls.open_arena.restype = ctypes.c_void_p
        cls.close_arena = cls.library.iso_bundle_test_arena_close
        cls.close_arena.argtypes = [ctypes.c_void_p]
        cls.allocate = cls.library.ctool_arena_alloc
        cls.allocate.argtypes = [ctypes.c_void_p, ctypes.c_uint, ctypes.c_uint,
                                 ctypes.POINTER(ctypes.c_void_p)]
        cls.mark = cls.library.ctool_arena_mark
        cls.mark.argtypes = [ctypes.c_void_p]
        cls.mark.restype = Mark
        cls.rewind = cls.library.ctool_arena_rewind
        cls.rewind.argtypes = [ctypes.c_void_p, Mark]
        cls.api = cls.library.iso_bundle_test_api
        cls.api.restype = ctypes.c_int
        cls.program = os.environ.get("CUPID_ISO_BUNDLE_CODEC_PROGRAM")

    @classmethod
    def unload_library(cls):
        import _ctypes
        _ctypes.FreeLibrary(cls.library._handle)
        cls.library._handle = 0

    def mark_values(self, mark):
        return mark.owner, mark.block, mark.used, mark.generation

    def call(self, payload, capacity=512, limit=4194304, roundtrip=True):
        buffer = ctypes.create_string_buffer(payload)
        source = Source(String(b"request.bundle", 14),
                        Bytes(ctypes.addressof(buffer), len(payload)))
        arena = self.open_arena(limit)
        self.assertTrue(arena)
        prefix = ctypes.c_void_p()
        self.assertEqual(self.allocate(arena, 37, 1, ctypes.byref(prefix)), 0)
        ctypes.memset(prefix, 0x5a, 37)
        before = self.mark_values(self.mark(arena))
        error = ctypes.create_string_buffer(b"?" * (capacity + 8))
        decoded = Decoded()
        ctypes.memset(ctypes.byref(decoded), 0x99, ctypes.sizeof(decoded))
        values = (0, 0, 0, 0, 0)
        rows = []
        try:
            result = self.decode(arena, ctypes.byref(source), ctypes.byref(decoded),
                                 error if capacity else None, capacity)
            if result:
                self.assertEqual(string_bytes(decoded.manifest.path), b"request.bundle")
                manifest = ctypes.string_at(decoded.manifest.contents.data,
                                            decoded.manifest.contents.size)
                start = ctypes.addressof(buffer)
                stop = start + len(payload)
                self.assertGreaterEqual(decoded.manifest.contents.data, start)
                self.assertLessEqual(decoded.manifest.contents.data + len(manifest), stop)
                for index in range(decoded.inventory.entry_count):
                    entry = decoded.inventory.entries[index]
                    name = string_bytes(entry.path)
                    if entry.kind == 1:
                        self.assertFalse(entry.source)
                        data = None
                    else:
                        self.assertEqual(entry.kind, 2)
                        file = entry.source.contents
                        data = ctypes.string_at(file.contents.data, file.contents.size)
                        self.assertGreaterEqual(file.contents.data, start)
                        self.assertLessEqual(file.contents.data + file.contents.size, stop)
                        self.assertEqual(string_bytes(file.path), name)
                    rows.append((name, data))
                encoded = Bytes()
                if roundtrip:
                    result = self.encode(arena, ctypes.byref(decoded.manifest),
                                         ctypes.byref(decoded.inventory), ctypes.byref(encoded),
                                         error if capacity else None, capacity)
                    if result:
                        self.assertEqual(ctypes.string_at(encoded.data, encoded.size), payload)
                    else:
                        self.assertFalse(encoded.data)
                        self.assertEqual(encoded.size, 0)
                values = (len(rows), len(manifest), sum(len(data) for _, data in rows if data is not None),
                          digest(rows, manifest), encoded.size)
                self.assertEqual(self.rewind(arena, self.mark_from(before)), 0)
            else:
                self.assertEqual(bytes(decoded), bytes(ctypes.sizeof(decoded)))
            self.assertEqual(self.mark_values(self.mark(arena)), before)
            self.assertEqual(ctypes.string_at(prefix, 37), b"Z" * 37)
            self.assertEqual(buffer.raw[:len(payload)], payload)
            self.assertEqual(error.raw[capacity:capacity + 8], b"?" * 8)
            diagnostic = error.value.decode("ascii") if capacity else ""
        finally:
            self.close_arena(arena)
        if self.program:
            with tempfile.TemporaryDirectory(prefix="iso-bundle-checked-") as directory:
                path = Path(directory) / "request.bundle"
                path.write_bytes(payload)
                checked = subprocess.run([self.program, str(path), str(capacity), str(limit),
                                          str(int(roundtrip))], capture_output=True,
                                         text=True, timeout=60)
                self.assertIn(checked.returncode, (0, 1), checked.stdout + checked.stderr)
                lines = checked.stdout.splitlines()
                self.assertEqual(tuple(map(int, lines[0].split())), values)
                self.assertEqual(lines[1] if len(lines) > 1 else "", diagnostic)
                self.assertEqual(checked.returncode, 1 - result)
        return result, values, diagnostic, rows

    def mark_from(self, values):
        return Mark(*values)

    def check(self, payload, error=None, **options):
        result, values, diagnostic, rows = self.call(payload, **options)
        self.assertEqual(result, int(error is None), diagnostic)
        if error is not None:
            self.assertIn(error, diagnostic)
        return values, rows

    def test_exact_roundtrip_preserves_order_kinds_empty_files_and_payload_bytes(self):
        rows = [(b"d/x", b"\x00\xff\r\n"), (b"d", None), (b"empty", b"")]
        for manifest in (b"d\nd/x\nempty\n", b"d\r\nd/x\r\nempty", b"empty\nd/x\nd"):
            values, decoded = self.check(wire(rows, manifest))
            self.assertEqual(decoded, rows)
            self.assertEqual(values[:3], (3, len(manifest), 4))

    def test_full_512_entries_and_maximum_metadata_roundtrip(self):
        rows = [(f"f{index:03}_".encode() + b"a" * 1018, bytes([index % 256]))
                for index in range(512)]
        manifest = b"x" * MAX_MANIFEST
        values, decoded = self.check(wire(rows, manifest))
        self.assertEqual(values[:3], (512, MAX_MANIFEST, 512))
        self.assertEqual(decoded, rows)

    def test_maximum_directory_count_and_zero_payload(self):
        rows = [(f"d{index:03}".encode(), None) for index in range(512)]
        self.assertEqual(self.check(wire(rows))[1], rows)

    def test_semantic_names_remain_views_for_the_producer_to_validate(self):
        rows = [(b"../bad\x00name", b"payload"), (b"missing/parent/file", b"")]
        self.assertEqual(self.check(wire(rows))[1], rows)

    def test_every_truncated_prefix_is_rejected_without_allocations(self):
        payload = wire([(b"d", None), (b"d/f", b"abc")])
        for size in range(len(payload)):
            with self.subTest(size=size):
                result, _, error, _ = self.call(payload[:size])
                self.assertEqual(result, 0)
                self.assertTrue(error)

    def test_wrong_magic_zero_or_oversized_manifest_and_count(self):
        valid = wire([(b"f", b"x")])
        for offset, value, reason in ((8, 0, "bounds"), (8, MAX_MANIFEST + 1, "bounds"),
                                     (8, 0xffffffff, "bounds"), (12, 0, "bounds"),
                                     (12, 513, "bounds"), (12, 0xffffffff, "bounds")):
            changed = bytearray(valid)
            struct.pack_into("<I", changed, offset, value)
            self.check(bytes(changed), reason)
        self.check(b"BADMAGIC" + valid[8:], "header")

    def test_unknown_kind_reserved_word_and_invalid_name_lengths(self):
        valid = wire([(b"f", b"x")])
        offset = 18
        for relative, value in ((0, 0), (0, 3), (0, 0xffffffff), (4, 0),
                                (4, 1024), (4, 0xffffffff), (12, 1)):
            changed = bytearray(valid)
            struct.pack_into("<I", changed, offset + relative, value)
            self.check(bytes(changed), "header")

    def test_directory_payload_and_overflowing_file_payload_are_rejected(self):
        directory = bytearray(wire([(b"d", None)]))
        struct.pack_into("<I", directory, 26, 1)
        self.check(bytes(directory) + b"x", "header")
        file = bytearray(wire([(b"f", b"x")]))
        struct.pack_into("<I", file, 26, 0xffffffff)
        self.check(bytes(file), "payload")

    def test_trailing_bytes_and_missing_declared_entries_are_rejected(self):
        valid = wire([(b"f", b"x")])
        self.check(valid + b"\0", "trailing")
        changed = bytearray(valid)
        struct.pack_into("<I", changed, 12, 2)
        self.check(bytes(changed), "header")

    def test_diagnostic_zero_one_and_short_capacity_preserve_canaries(self):
        for capacity in (0, 1, 8, 512):
            result, _, error, _ = self.call(b"bad", capacity=capacity)
            self.assertEqual(result, 0)
            self.assertLessEqual(len(error), max(0, capacity - 1))

    def test_decode_and_encode_allocation_failures_rewind_existing_prefix(self):
        # 4096 bytes can hold the prefix and entry arrays, but not this payload.
        payload = wire([(b"f", b"x" * 8192)])
        self.check(payload, "allocation", limit=4096)
        self.check(payload, limit=4096, roundtrip=False)
        rows = [(f"f{index:03}".encode(), b"") for index in range(512)]
        self.check(wire(rows), "allocation", limit=4096)
        self.check(wire(rows))

    def test_api_null_arguments_invalid_views_and_u32_overflow_preflight(self):
        self.assertEqual(self.api(), 0)
        if self.program:
            checked = subprocess.run([self.program, "--api"], capture_output=True, timeout=60)
            self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)

    def test_concurrent_calls_have_independent_arena_owned_arrays(self):
        rows = [(b"d", None), (b"d/file", b"\0" * 3000)]
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
            futures = [executor.submit(self.check, wire(rows)) for _ in range(16)]
            for future in futures:
                self.assertEqual(future.result()[1], rows)


class IsoFixtureBundleCliTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="iso-bundle-cli-")
        cls.addClassCleanup(cls.build.cleanup)
        cls.native = _build_cli(Path(cls.build.name), "cupidobj", [
            "ctool.cc", "ctool_host.cc", "elf32.cc", "cupidobj.cc",
            "iso_fixture_bundle.cc", "cupidobj_main.cc"])
        cls.programs = [cls.native]
        checked = os.environ.get("CUPIDOBJ_ISO_BUNDLE_PROGRAM")
        if checked:
            cls.programs.append(Path(checked))

    def run_bundle(self, rows, manifest=None, payload=None, arguments=(), error=None):
        for program in self.programs:
            with self.subTest(program=str(program)), tempfile.TemporaryDirectory(prefix="iso-bundle-producer-") as directory:
                root = Path(directory)
                bundle = root / "request.bundle"
                bundle.write_bytes(wire(rows, manifest) if payload is None else payload)
                output = root / "result.iso"
                sentinel = b"existing image survives rejected bundle"
                output.write_bytes(sentinel)
                before = output.stat()
                command = [str(program), "iso-fixture-bundle", str(bundle), "-o", str(output), *arguments]
                self.assertLess(len(subprocess.list2cmdline(command)), 32767)
                result = subprocess.run(command, cwd=root, capture_output=True, timeout=60)
                if error is None:
                    self.assertEqual(result.returncode, 0, result.stderr.decode(errors="replace"))
                    self.assertEqual(output.read_bytes(), image(rows))
                else:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(error.encode(), result.stderr)
                    self.assertEqual(output.read_bytes(), sentinel)
                    self.assertEqual(output.stat().st_mtime_ns, before.st_mtime_ns)
                self.assertEqual(sorted(path.name for path in root.iterdir()), ["request.bundle", "result.iso"])

    def test_active_fixture_matches_independent_python_image(self):
        rows = []
        for path in (ROOT / "test_iso/fixtures.manifest").read_text().splitlines():
            native = ROOT / "test_iso/fixtures" / path
            payload = bytes(range(256)) * 16 if path == "big.bin" else (
                None if native.is_dir() else native.read_bytes())
            rows.append((path.encode("ascii"), payload))
        self.run_bundle(rows)

    def test_complete_512_long_names_use_bounded_command_and_no_native_payload_paths(self):
        rows = [(f"f{index:03}_".encode() + b"a" * 122, bytes([index % 256]))
                for index in range(512)]
        self.run_bundle(rows)

    def test_collisions_order_crlf_and_empty_payloads_match_python(self):
        rows = [(b"z.TXT", b"z"), (b"d", None), (b"d/a.txt", b""),
                (b"d/long_file_name.txt", b"x" * 2049), (b"d/longfile_name.txt", b"y")]
        manifest = b"\r\n".join(name for name, _ in rows)
        self.run_bundle(rows, manifest)
        self.run_bundle(list(reversed(rows)), manifest)

    def test_malformed_bundle_is_useful_and_preserves_existing_image(self):
        rows = [(b"f", b"x")]
        for payload, error in ((b"bad", "header"), (wire(rows)[:-1], "payload"),
                               (wire(rows) + b"\0", "trailing")):
            self.run_bundle(rows, payload=payload, error=error)

    def test_producer_rejects_missing_parents_and_manifest_mismatch(self):
        self.run_bundle([(b"missing/file", b"x")], error="parent")
        self.run_bundle([(b"f", b"x")], manifest=b"g\n", error="manifest")
        self.run_bundle([(b"../escape", b"x")], error="path")

    def test_bundle_mode_rejects_mixed_native_file_and_directory_options(self):
        for arguments in (("--file", "f", "missing-file"), ("--directory", "d")):
            self.run_bundle([(b"f", b"x")], arguments=arguments, error="usage")

    @unittest.skipIf(os.name == "nt", "Inherited output descriptors are POSIX-only")
    def test_bundle_uses_existing_inherited_output_descriptor(self):
        rows = [(b"f", b"payload")]
        for program in self.programs:
            with tempfile.TemporaryDirectory(prefix="iso-bundle-descriptor-") as directory:
                root = Path(directory)
                bundle = root / "request.bundle"
                bundle.write_bytes(wire(rows))
                result = subprocess.run([str(program), "iso-fixture-bundle", str(bundle),
                                         "-o", "/dev/fd/1"], cwd=root, capture_output=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, image(rows))
                self.assertEqual(list(root.iterdir()), [bundle])

"""Native syscall ABI bytes and retained-source verification against the oracle."""
import concurrent.futures
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tools import user_syscall_abi as oracle


ROOT = Path(__file__).resolve().parents[1]


class Input(ctypes.Structure):
    _fields_ = [("bytes", ctypes.POINTER(ctypes.c_ubyte)), ("size", ctypes.c_size_t)]


class Report(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint) for name in (
        "version", "field_count", "table_size", "dirent_size", "dirent_name_offset",
        "dirent_value_offset", "dirent_type_offset", "stat_size", "stat_value_offset",
        "stat_type_offset", "provider_count",
    )] + [(name, ctypes.c_char * size) for name, size in (
        ("first_function", 64), ("last_function", 64),
        ("abi_sha256", 65), ("provider_sha256", 65),
    )]


class CupidBuildUserAbiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="native-user-abi-")
        cls.addClassCleanup(cls.build.cleanup)
        directory = Path(cls.build.name)
        compiler = shutil.which("clang" if os.name == "nt" else "cc")
        cls.library_path = directory / ("user-abi.dll" if os.name == "nt" else "user-abi.so")
        exports = ["-Wl,/export:" + name for name in (
            "cupid_user_abi_validate", "cupid_user_abi_format_json", "cupid_user_abi_input_path",
            "cupidbuild_verify_user_abi",
        )] if os.name == "nt" else ["-fPIC"]
        sources = [ROOT / "toolchain" / (name + ".cc") for name in (
            "user_syscall_abi", "cupidbuild_user_abi", "cupidbuild_host", "path_encoding",
        )]
        command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                   "-D_CRT_SECURE_NO_WARNINGS", "-shared", "-x", "c", "-I",
                   str(ROOT / "toolchain"), *map(str, sources), *exports]
        if os.name == "nt":
            command.append("-lntdll")
        result = subprocess.run([*command, "-o", str(cls.library_path)],
                                capture_output=True, text=True, timeout=120)
        if result.returncode:
            raise AssertionError(result.stderr)
        cls.library = ctypes.CDLL(str(cls.library_path))
        if os.name == "nt":
            cls.addClassCleanup(cls.unload_library)
        cls.validate = cls.library.cupid_user_abi_validate
        cls.validate.argtypes = [ctypes.POINTER(Input), ctypes.POINTER(Report), ctypes.c_void_p, ctypes.c_size_t]
        cls.validate.restype = ctypes.c_int
        cls.format = cls.library.cupid_user_abi_format_json
        cls.format.argtypes = [ctypes.POINTER(Report), ctypes.c_void_p, ctypes.c_size_t]
        cls.format.restype = ctypes.c_int
        cls.path = cls.library.cupid_user_abi_input_path
        cls.path.argtypes = [ctypes.c_size_t]
        cls.path.restype = ctypes.c_char_p
        cls.verify = cls.library.cupidbuild_verify_user_abi
        cls.verify.argtypes = [ctypes.c_char_p, ctypes.POINTER(Report), ctypes.c_void_p, ctypes.c_size_t]
        cls.verify.restype = ctypes.c_int
        common = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                  "-D_CRT_SECURE_NO_WARNINGS", "-I", str(ROOT / "toolchain"), "-x", "c"]
        fault_object = directory / "semantic-fault.o"
        observer_object = directory / "observer-fault.o"
        jobs = (
            [*common, "-Dmalloc=abi_probe_malloc", "-Dcalloc=abi_probe_calloc",
             "-Dfree=abi_probe_free", "-c", str(ROOT / "toolchain/user_syscall_abi.cc"), "-o", str(fault_object)],
            [*common, "-Dcupidbuild_host_observer_require_unchanged=abi_probe_observer_unchanged",
             "-Dcupidbuild_host_observer_close=abi_probe_observer_close", "-c",
             str(ROOT / "toolchain/cupidbuild_user_abi.cc"), "-o", str(observer_object)],
        )
        if os.name != "nt":
            jobs = tuple([*job[:1], "-fPIC", *job[1:]] for job in jobs)
        for job in jobs:
            result = subprocess.run(job, capture_output=True, text=True, timeout=120)
            if result.returncode: raise AssertionError(result.stderr)
        fault_exports = ("cupid_user_abi_validate", "abi_probe_begin", "abi_probe_calls", "abi_probe_live")
        observer_exports = ("cupidbuild_verify_user_abi", "abi_probe_observer_configure")
        for label, object_path, probe, extra_sources, names in (
            ("allocation", fault_object, "user_abi_allocation_probe", (), fault_exports),
            ("observer", observer_object, "user_abi_observer_probe",
             ("user_syscall_abi", "cupidbuild_host", "path_encoding"), observer_exports),
        ):
            destination = directory / (label + (".dll" if os.name == "nt" else ".so"))
            flags = ["-Wl,/export:" + name for name in names] if os.name == "nt" else ["-fPIC"]
            result = subprocess.run([
                *common, "-shared", str(ROOT / "toolchain/tests" / (probe + ".cc")),
                *(str(ROOT / "toolchain" / (name + ".cc")) for name in extra_sources),
                "-x", "none", str(object_path), *flags,
                *(["-lntdll"] if os.name == "nt" and label == "observer" else []),
                "-o", str(destination),
            ], capture_output=True, text=True, timeout=120)
            if result.returncode: raise AssertionError(result.stderr)
            library = ctypes.CDLL(str(destination))
            setattr(cls, label + "_library", library)
            if os.name == "nt": cls.addClassCleanup(cls.unload_extra_library, library)
        cls.allocation_validate = cls.allocation_library.cupid_user_abi_validate
        cls.allocation_validate.argtypes = cls.validate.argtypes
        cls.allocation_validate.restype = ctypes.c_int
        cls.race_verify = cls.observer_library.cupidbuild_verify_user_abi
        cls.race_verify.argtypes = cls.verify.argtypes
        cls.race_verify.restype = ctypes.c_int
        cls.configure_race = cls.observer_library.abi_probe_observer_configure
        cls.configure_race.argtypes = [ctypes.c_int, ctypes.c_char_p]
        cls.configure_race.restype = ctypes.c_int

    @classmethod
    def unload_library(cls):
        import _ctypes
        _ctypes.FreeLibrary(cls.library._handle)
        cls.library._handle = 0

    @staticmethod
    def unload_extra_library(library):
        import _ctypes
        _ctypes.FreeLibrary(library._handle)
        library._handle = 0

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="user-abi-source-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.payloads = [(ROOT / path).read_bytes() for path in oracle.ABI_INPUTS]
        for path, payload in zip(oracle.ABI_INPUTS, self.payloads):
            target = self.root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(payload)

    def call_bytes(self, payloads=None, error_size=1024):
        payloads = self.payloads if payloads is None else payloads
        # Exact-sized buffers prove callers need not supply trailing terminators.
        storage = [(ctypes.c_ubyte * len(payload)).from_buffer_copy(payload) for payload in payloads]
        inputs = (Input * 6)(*(Input(data, len(payload)) for data, payload in zip(storage, payloads)))
        report = Report()
        ctypes.memset(ctypes.byref(report), 0xA5, ctypes.sizeof(report))
        error = ctypes.create_string_buffer(b"Z" * (error_size + 8), error_size + 9)
        before = [bytes(data) for data in storage]
        ok = self.validate(inputs, ctypes.byref(report), ctypes.byref(error, 4), error_size)
        self.assertEqual([bytes(data) for data in storage], before)
        self.assertEqual(error.raw[:4], b"ZZZZ")
        self.assertEqual(error.raw[4 + error_size:8 + error_size], b"ZZZZ")
        message = error.raw[4:4 + error_size].split(b"\0", 1)[0].decode()
        if not ok:
            self.assertEqual(bytes(report), b"\0" * ctypes.sizeof(report))
        return ok, report, message

    def json_report(self, report):
        output = ctypes.create_string_buffer(2048)
        self.assertEqual(self.format(ctypes.byref(report), output, len(output)), 1)
        return json.loads(output.value)

    def test_borrowed_bytes_match_full_independent_oracle_report(self):
        ok, report, error = self.call_bytes()
        self.assertEqual((ok, error), (1, ""))
        self.assertEqual(self.json_report(report), oracle.check_syscall_abi(self.root))
        self.assertEqual([self.path(index).decode() for index in range(6)], list(oracle.ABI_INPUTS))
        self.assertIsNone(self.path(6))
        self.assertIsNone(self.path(ctypes.c_size_t(-1).value))

    def test_each_semantic_boundary_rejects_and_recovers(self):
        mutations = (
            (5, b"#define CUPID_SYSCALL_VERSION 5", b"#define CUPID_SYSCALL_VERSION 4", "version differs"),
            (5, b"void (*print_int)(uint32_t num);", b"void (*print_int)(uint16_t num);", "field 4 differs"),
            (5, b"typedef unsigned long      size_t;", b"typedef unsigned long long size_t;", "size_t differs"),
            (5, b"#define VFS_MAX_NAME    128", b"#define VFS_MAX_NAME    64", "VFS_MAX_NAME differs"),
            (5, b"uint8_t  type;\n} cupid_dirent_t;", b"uint16_t type;\n} cupid_dirent_t;", "does not match"),
            (5, b"#define SOCK_TCP       2", b"#define SOCK_TCP       3", "SOCK_TCP differs"),
            (2, b"syscall_table.print_hex = print_hex;", b"", "print_hex"),
            (2, b"syscall_table.print_hex = print_hex;", b"syscall_table.print_hex = print_int;", "provider contract changed"),
        )
        for index, old, new, diagnostic in mutations:
            with self.subTest(diagnostic=diagnostic):
                payloads = list(self.payloads)
                payloads[index] = payloads[index].replace(b"\r\n", b"\n")
                self.assertIn(old, payloads[index])
                payloads[index] = payloads[index].replace(old, new)
                ok, _, error = self.call_bytes(payloads)
                self.assertEqual(ok, 0)
                self.assertIn(diagnostic, error)
                self.assertEqual(self.call_bytes()[0], 1)

    def test_matching_unreviewed_versions_fail(self):
        payloads = list(self.payloads)
        for index in (1, 5):
            payloads[index] = payloads[index].replace(b"#define CUPID_SYSCALL_VERSION 5", b"#define CUPID_SYSCALL_VERSION 6")
        self.assertIn("not the reviewed version", self.call_bytes(payloads)[2])

    def test_utf8_comments_accept_all_widths_and_reject_invalid_encodings(self):
        payloads = list(self.payloads)
        payloads[0] += "\n/* café 日本語 😀 */\n".encode()
        self.assertEqual(self.call_bytes(payloads)[0], 1)
        for suffix in (b"\0", b"\xc0\x80", b"\xe0\x80\x80", b"\xed\xa0\x80",
                       b"\xf0\x80\x80\x80", b"\xf4\x90\x80\x80", b"\xf5\x80\x80\x80",
                       b"\xc2", b"\xe2\x82", b"\xf0\x9f\x98", b"\x80"):
            with self.subTest(suffix=suffix):
                payloads[0] = self.payloads[0] + b"\n/*" + suffix + b"*/\n"
                ok, _, error = self.call_bytes(payloads)
                self.assertEqual(ok, 0)
                self.assertIn("not NUL-free UTF-8", error)

    def test_source_and_token_limits_reject_without_partial_results(self):
        payloads = list(self.payloads)
        payloads[0] += b"\n/*" + b" " * (1024 * 1024 - len(payloads[0]) - 6) + b"*/\n"
        self.assertEqual(len(payloads[0]), 1024 * 1024)
        self.assertEqual(self.call_bytes(payloads)[0], 1)
        payloads[0] += b" "
        self.assertIn("unsupported size", self.call_bytes(payloads)[2])
        payloads[0] = self.payloads[0] + b" x" * 32768
        self.assertIn("too many tokens", self.call_bytes(payloads)[2])
        payloads[0] = self.payloads[0] + b" /* unterminated"
        self.assertIn("incomplete comment", self.call_bytes(payloads)[2])
        for tail in (b' "unterminated', b' "', b" '", b' "\\"', b" '\\'"):
            with self.subTest(tail=tail):
                payloads[0] = self.payloads[0] + tail
                self.assertIn("incomplete string", self.call_bytes(payloads)[2])
                self.assertEqual(self.call_bytes()[0], 1)
        payloads[0] = self.payloads[0] + b"\nstatic const char *abi_quote = \"\\\"\";\nstatic char abi_character = '\\'';\n"
        self.assertEqual(self.call_bytes(payloads)[0], 1)

    def test_null_storage_and_small_diagnostics_remain_bounded(self):
        result = Report()
        error = ctypes.create_string_buffer(32)
        for inputs, output in ((None, ctypes.byref(result)), (None, None)):
            self.assertEqual(self.validate(inputs, output, error, len(error)), 0)
        for capacity in (0, 1, 2, 16):
            payloads = list(self.payloads)
            payloads[0] = b""
            self.assertEqual(self.call_bytes(payloads, capacity)[0], 0)
        self.assertEqual(self.validate(None, None, None, 0), 0)
        self.assertEqual(self.verify(None, ctypes.byref(result), error, len(error)), 0)
        self.assertEqual(bytes(result), b"\0" * ctypes.sizeof(result))

    def test_json_output_capacity_and_null_report_fail_cleanly(self):
        _, report, _ = self.call_bytes()
        for size in (0, 1, 2, 32):
            output = ctypes.create_string_buffer(b"Z" * 64, 65)
            self.assertEqual(self.format(ctypes.byref(report), output, size), 0)
            self.assertEqual(output.raw[size:], b"Z" * (64 - size) + b"\0")
            if size:
                self.assertEqual(output.raw[0], 0)
        output = ctypes.create_string_buffer(b"stale")
        self.assertEqual(self.format(None, output, len(output)), 0)
        self.assertEqual(output.value, b"")

    def test_concurrent_validation_keeps_failures_and_reports_independent(self):
        bad = list(self.payloads)
        bad[0] += b"\0"
        def run(index):
            ok, report, error = self.call_bytes(bad if index % 2 else None)
            return ok, report.field_count, bool(error)
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
            results = list(executor.map(run, range(24)))
        self.assertEqual(results, [(0, 0, True) if index % 2 else (1, 103, False) for index in range(24)])

    def test_retained_inputs_match_oracle_without_namespace_changes(self):
        before = {str(path.relative_to(self.root)): (path.stat().st_size, path.stat().st_mtime_ns)
                  for path in self.root.rglob("*")}
        result = Report()
        error = ctypes.create_string_buffer(1024)
        self.assertEqual(self.verify(os.fsencode(self.root), ctypes.byref(result), error, len(error)), 1, error.value)
        self.assertEqual(self.json_report(result), oracle.check_syscall_abi(self.root))
        after = {str(path.relative_to(self.root)): (path.stat().st_size, path.stat().st_mtime_ns)
                 for path in self.root.rglob("*")}
        self.assertEqual(before, after)

    def test_retained_verifier_rejects_missing_nonfiles_and_oversized_inputs(self):
        path = self.root / oracle.ABI_INPUTS[-1]
        for replacement in ("missing", "directory", "large"):
            with self.subTest(replacement=replacement):
                path.unlink(missing_ok=True)
                if replacement == "directory": path.mkdir()
                if replacement == "large": path.write_bytes(b" " * (1024 * 1024 + 1))
                result = Report()
                ctypes.memset(ctypes.byref(result), 0xA5, ctypes.sizeof(result))
                error = ctypes.create_string_buffer(1024)
                self.assertEqual(self.verify(os.fsencode(self.root), ctypes.byref(result), error, len(error)), 0)
                self.assertTrue(error.value)
                self.assertEqual(bytes(result), b"\0" * ctypes.sizeof(result))
                if path.is_dir(): path.rmdir()
                elif path.exists(): path.unlink()
                path.write_bytes(self.payloads[-1])
                self.assertEqual(self.verify(os.fsencode(self.root), ctypes.byref(result), error, len(error)), 1)

    def test_every_semantic_allocation_failure_releases_storage_and_recovers(self):
        storage = [(ctypes.c_ubyte * len(payload)).from_buffer_copy(payload) for payload in self.payloads]
        inputs = (Input * 6)(*(Input(data, len(payload)) for data, payload in zip(storage, self.payloads)))
        report = Report()
        error = ctypes.create_string_buffer(1024)
        self.allocation_library.abi_probe_begin(0)
        self.assertEqual(self.allocation_validate(inputs, ctypes.byref(report), error, len(error)), 1)
        calls = self.allocation_library.abi_probe_calls()
        self.assertGreater(calls, 20)
        self.assertEqual(self.allocation_library.abi_probe_live(), 0)
        for denied in range(1, calls + 1):
            with self.subTest(denied=denied):
                self.allocation_library.abi_probe_begin(denied)
                self.assertEqual(self.allocation_validate(inputs, ctypes.byref(report), error, len(error)), 0)
                self.assertTrue(error.value)
                self.assertEqual(bytes(report), b"\0" * ctypes.sizeof(report))
                self.assertEqual(self.allocation_library.abi_probe_live(), 0)
                self.allocation_library.abi_probe_begin(0)
                self.assertEqual(self.allocation_validate(inputs, ctypes.byref(report), error, len(error)), 1)
                self.assertEqual(self.allocation_library.abi_probe_live(), 0)

    def test_real_post_validation_drift_and_close_failure_clear_the_result(self):
        path = self.root / oracle.ABI_INPUTS[-1]
        original = path.read_bytes()
        for mode in (1, 2):
            with self.subTest(mode=mode):
                self.assertEqual(self.configure_race(mode, os.fsencode(path)), 1)
                result = Report()
                error = ctypes.create_string_buffer(1024)
                self.assertEqual(self.race_verify(os.fsencode(self.root), ctypes.byref(result), error, len(error)), 0)
                self.assertTrue(error.value)
                self.assertEqual(bytes(result), b"\0" * ctypes.sizeof(result))
                if mode == 1: self.assertNotEqual(path.read_bytes(), original)
                path.write_bytes(original)
                self.assertEqual(self.configure_race(0, None), 1)
                self.assertEqual(self.race_verify(os.fsencode(self.root), ctypes.byref(result), error, len(error)), 1)


if __name__ == "__main__":
    unittest.main()

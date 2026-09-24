import concurrent.futures
import ctypes
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

from tests.test_artifact_size_policy_contract import _host_compiler, _request


ROOT = Path(__file__).resolve().parents[1]


class PolicyResult(ctypes.Structure):
    _fields_ = [
        ("artifact_count", ctypes.c_uint32),
        ("total_exact_bytes", ctypes.c_uint64),
    ]


class ArtifactSizePolicyApiTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="cupid-policy-api-")
        cls.addClassCleanup(cls.temporary.cleanup)
        build = Path(cls.temporary.name)
        shim = build / "api_shim.c"
        shim.write_text(
            '#include "artifact_size_policy.h"\n'
            "#if defined(_WIN32)\n__declspec(dllexport)\n#endif\n"
            "int policy_test(const unsigned char *bytes, size_t size,\n"
            "                artifact_size_policy_result_t *result,\n"
            "                char *error, size_t capacity) {\n"
            "  return artifact_size_policy_validate(bytes, size, result, error, capacity);\n"
            "}\n",
            encoding="utf-8",
        )
        library = build / ("policy.dll" if os.name == "nt" else "policy.so")
        command = [
            _host_compiler(), "-std=c11", "-O2", "-shared",
            "-I", str(ROOT / "toolchain"), "-x", "c", str(shim),
            str(ROOT / "toolchain/artifact_size_policy.cc"),
            str(ROOT / "toolchain/contract_parse_internal.cc"),
            "-o", str(library),
        ]
        if os.name != "nt":
            command.append("-fPIC")
        built = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)
        cls.library = ctypes.CDLL(str(library))
        if os.name == "nt":
            cls.addClassCleanup(cls.unload_library)
        cls.api = cls.library.policy_test
        cls.api.argtypes = [
            ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(PolicyResult),
            ctypes.c_void_p, ctypes.c_size_t,
        ]
        cls.api.restype = ctypes.c_int

    @classmethod
    def unload_library(cls):
        import _ctypes
        _ctypes.FreeLibrary(cls.library._handle)
        cls.library._handle = 0

    def call(self, payload, capacity=512):
        request = ctypes.create_string_buffer(payload)
        before = bytes(request)
        result = PolicyResult(99, 99)
        error = ctypes.create_string_buffer(b"!" * (capacity + 8))
        status = self.api(request, len(payload), ctypes.byref(result), error, capacity)
        self.assertEqual(bytes(request), before)
        self.assertEqual(bytes(error)[capacity:], b"!" * 8 + b"\0")
        return status, result, error

    def test_valid_request_returns_facts_and_clears_diagnostic(self):
        status, result, error = self.call(_request())
        self.assertEqual(status, 1)
        self.assertEqual((result.artifact_count, result.total_exact_bytes), (16, 1042))
        self.assertEqual(error.value, b"")

    def test_early_and_late_failure_clear_result_without_changing_input(self):
        for payload, message in (
            (b"bad", b"request magic differs from CUPSIZE2"),
            (_request() + b"x", b"request has trailing input"),
        ):
            with self.subTest(message=message):
                status, result, error = self.call(payload)
                self.assertEqual(status, 0)
                self.assertEqual((result.artifact_count, result.total_exact_bytes), (0, 0))
                self.assertEqual(error.value, message)

    def test_diagnostic_capacity_preserves_adjacent_storage(self):
        message = b"request magic differs from CUPSIZE2"
        for capacity in (0, 1, 8, len(message), len(message) + 1):
            with self.subTest(capacity=capacity):
                status, result, error = self.call(b"bad", capacity)
                self.assertEqual(status, 0)
                self.assertEqual(result.artifact_count, 0)
                if capacity:
                    self.assertEqual(error.value, message[:capacity - 1])

    def test_null_arguments_fail_without_stale_results(self):
        request = ctypes.create_string_buffer(_request())
        result = PolicyResult(99, 99)
        error = ctypes.create_string_buffer(128)
        self.assertEqual(self.api(None, 1, ctypes.byref(result), error, len(error)), 0)
        self.assertEqual((result.artifact_count, result.total_exact_bytes), (0, 0))
        self.assertEqual(error.value, b"invalid artifact-size policy API arguments")
        self.assertEqual(self.api(request, len(request) - 1, None, error, len(error)), 0)
        result = PolicyResult(99, 99)
        self.assertEqual(self.api(request, len(request) - 1, ctypes.byref(result), None, 1), 0)
        self.assertEqual((result.artifact_count, result.total_exact_bytes), (0, 0))

    def test_optional_diagnostic_and_recovery(self):
        request = ctypes.create_string_buffer(_request())
        result = PolicyResult(99, 99)
        self.assertEqual(self.api(None, 0, ctypes.byref(result), None, 0), 0)
        self.assertEqual(self.api(request, len(request) - 1, ctypes.byref(result), None, 0), 1)
        self.assertEqual((result.artifact_count, result.total_exact_bytes), (16, 1042))

    def test_concurrent_calls_keep_independent_results_and_errors(self):
        valid = _request()

        def exercise(index):
            if index % 3 == 0:
                status, result, error = self.call(valid)
                return status == 1 and result.total_exact_bytes == 1042 and error.value == b""
            payload = b"bad" if index % 3 == 1 else valid + b"x"
            expected = b"request magic differs from CUPSIZE2" if index % 3 == 1 else b"request has trailing input"
            status, result, error = self.call(payload)
            return status == 0 and result.artifact_count == result.total_exact_bytes == 0 and error.value == expected

        # CDLL releases the GIL. Each call owns its input, result, and diagnostic.
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            self.assertTrue(all(pool.map(exercise, range(384))))


if __name__ == "__main__":
    unittest.main()

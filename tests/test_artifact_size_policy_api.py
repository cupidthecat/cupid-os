import concurrent.futures
import copy
import ctypes
import os
from pathlib import Path
import subprocess
import struct
import tempfile
import unittest

from tests.test_artifact_size_policy_contract import (
    _host_compiler, _request, _manifest, _windows_manifest, _policy,
    _observations, _windows_observations, _append_bytes, _json_bytes,
    MANIFEST_PATH, WINDOWS_MANIFEST_PATH,
)


ROOT = Path(__file__).resolve().parents[1]


def observation_request(*, policy=None, manifest_path=MANIFEST_PATH,
                        windows_manifest_path=WINDOWS_MANIFEST_PATH,
                        linux_sizes=None, windows_identities=None,
                        windows_observations=None, observations=None):
    roles = ("cupidasm", "cupidc", "cupiddis", "cupidld", "cupidobj", "cupidbuild")
    linux = {row["name"]: row for row in _manifest()["artifacts"]}
    windows_manifest = _windows_manifest("a" * 64)
    windows = {row["name"]: row for row in windows_manifest["artifacts"]}
    if policy is None:
        policy = _policy(manifest_path)
    if linux_sizes is None:
        linux_sizes = [linux[role]["size"] for role in roles]
    if windows_identities is None:
        windows_identities = [(windows[role]["size"], windows[role]["sha256"]) for role in roles]
    if windows_observations is None:
        windows_observations = _windows_observations(windows_manifest)
    if observations is None:
        observations = _observations(policy)
    payload = bytearray(b"CUPSIZE3")
    for value in (_json_bytes(policy), manifest_path.encode(), windows_manifest_path.encode()):
        _append_bytes(payload, value)
    for size in linux_sizes:
        payload.extend(struct.pack("<Q", size))
    for size, digest in windows_identities:
        payload.extend(struct.pack("<Q", size))
        _append_bytes(payload, digest.encode())
    payload.extend(struct.pack("<I", len(windows_observations)))
    for path, kind, size, digest in windows_observations:
        _append_bytes(payload, path.encode())
        payload.extend(struct.pack("<IQ", kind, size))
        _append_bytes(payload, digest.encode())
    payload.extend(struct.pack("<I", len(observations)))
    for path, kind, size in observations:
        _append_bytes(payload, path.encode())
        payload.extend(struct.pack("<IQ", kind, size))
    return bytes(payload)


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
            "}\n"
            "#if defined(_WIN32)\n__declspec(dllexport)\n#endif\n"
            "int policy_observations_test(const unsigned char *bytes, size_t size,\n"
            "                artifact_size_policy_result_t *result,\n"
            "                char *error, size_t capacity) {\n"
            "  return artifact_size_policy_validate_observations(bytes, size, result, error, capacity);\n"
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
        cls.observation_api = cls.library.policy_observations_test
        cls.observation_api.argtypes = cls.api.argtypes
        cls.observation_api.restype = ctypes.c_int

    @classmethod
    def unload_library(cls):
        import _ctypes
        _ctypes.FreeLibrary(cls.library._handle)
        cls.library._handle = 0

    def call(self, payload, capacity=512, *, captured=False):
        request = ctypes.create_string_buffer(payload)
        before = bytes(request)
        result = PolicyResult(99, 99)
        error = ctypes.create_string_buffer(b"!" * (capacity + 8))
        api = self.observation_api if captured else self.api
        status = api(request, len(payload), ctypes.byref(result), error, capacity)
        self.assertEqual(bytes(request), before)
        self.assertEqual(bytes(error)[capacity:], b"!" * 8 + b"\0")
        return status, result, error

    def test_captured_policy_has_the_same_decision_and_a_separate_request_boundary(self):
        payload = observation_request()
        status, result, error = self.call(payload, captured=True)
        self.assertEqual((status, result.artifact_count, result.total_exact_bytes), (1, 16, 1042))
        self.assertEqual(error.value, b"")
        for wrong, captured, magic in ((payload, False, b"CUPSIZE2"), (_request(), True, b"CUPSIZE3")):
            status, result, error = self.call(wrong, captured=captured)
            self.assertEqual((status, result.artifact_count, result.total_exact_bytes), (0, 0, 0))
            self.assertEqual(error.value, b"request magic differs from " + magic)

    def test_captured_policy_rejects_every_truncation_and_trailing_input(self):
        payload = observation_request()
        for end in range(len(payload)):
            status, result, _ = self.call(payload[:end], captured=True)
            self.assertEqual((status, result.artifact_count, result.total_exact_bytes), (0, 0, 0), end)
        status, result, error = self.call(payload + b"x", captured=True)
        self.assertEqual((status, result.artifact_count, result.total_exact_bytes), (0, 0, 0))
        self.assertEqual(error.value, b"request has trailing input")

    def test_captured_seed_facts_observations_and_policy_remain_strict(self):
        roles = ("cupidasm", "cupidc", "cupiddis", "cupidld", "cupidobj", "cupidbuild")
        linux = {row["name"]: row["size"] for row in _manifest()["artifacts"]}
        windows_manifest = _windows_manifest("a" * 64)
        windows = {row["name"]: (row["size"], row["sha256"]) for row in windows_manifest["artifacts"]}
        cases = [dict(manifest_path="../seed.json"), dict(windows_manifest_path="other/manifest.json")]
        for index in range(6):
            for size in (0, 0x100000000):
                sizes = [linux[role] for role in roles]
                sizes[index] = size
                cases.append(dict(linux_sizes=sizes))
                identities = [windows[role] for role in roles]
                identities[index] = (size, identities[index][1])
                cases.append(dict(windows_identities=identities))
            for digest in ("", "A" * 64, "g" * 64, "0" * 63, "0" * 65, "0" * 64, "\0" * 64):
                identities = [windows[role] for role in roles]
                identities[index] = (identities[index][0], digest)
                cases.append(dict(windows_identities=identities))
        observations = _observations(_policy())
        win_observations = _windows_observations(windows_manifest)
        cases.extend([dict(observations=observations[:-1]),
                      dict(observations=[observations[0], *observations[:-1]]),
                      dict(windows_observations=win_observations[:-1]),
                      dict(windows_observations=[win_observations[0], *win_observations[:-1]])])
        for key, value in (("producer", "HostCompiler"), ("exact_bytes", 0), ("path", "other.bin")):
            policy = copy.deepcopy(_policy())
            policy["artifacts"][0][key] = value
            cases.append(dict(policy=policy))
        for case in cases:
            with self.subTest(case=case):
                status, result, _ = self.call(observation_request(**case), captured=True)
                self.assertEqual((status, result.artifact_count, result.total_exact_bytes), (0, 0, 0))
        self.assertEqual(self.call(observation_request(), captured=True)[0], 1)

    def test_captured_diagnostics_concurrency_null_arguments_and_recovery(self):
        payload = observation_request()
        for capacity in (0, 1, 2, 8, 128):
            status, result, error = self.call(payload + b"x", capacity, captured=True)
            self.assertEqual((status, result.artifact_count, result.total_exact_bytes), (0, 0, 0))
            if capacity:
                self.assertEqual(error.value, b"request has trailing input"[:capacity - 1])
            self.assertEqual(self.call(payload, capacity, captured=True)[0], 1)
        request = ctypes.create_string_buffer(payload)
        result = PolicyResult(99, 99)
        self.assertEqual(self.observation_api(request, len(payload), ctypes.byref(result), None, 0), 1)
        for data, size, output, error, capacity in (
            (None, 1, ctypes.byref(result), None, 0),
            (request, len(payload), None, None, 0),
            (request, len(payload), ctypes.byref(result), None, 1),
        ):
            self.assertEqual(self.observation_api(data, size, output, error, capacity), 0)
            self.assertEqual((result.artifact_count, result.total_exact_bytes), (0, 0))
        def exercise(index):
            status, facts, _ = self.call(payload + (b"x" if index % 2 else b""), captured=True)
            return (status, facts.artifact_count, facts.total_exact_bytes) == ((0, 0, 0) if index % 2 else (1, 16, 1042))
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            self.assertTrue(all(pool.map(exercise, range(384))))

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


    def test_failure_categories_are_aggregated_in_policy_order(self):
        from tests.test_artifact_size_policy_contract import _policy, _observations
        original = _observations(_policy())
        phrases = (" is unavailable or not a regular file", " is missing",
                   " is linked or reparse-backed", " has a non-directory parent",
                   " is not a regular file")
        rows = [(path, 2 + index % 5, 0) for index, (path, _, _) in enumerate(original)]
        expected = "".join("\n- " + path + phrases[kind - 2] for path, kind, _ in rows).encode()
        for selected in (rows, list(reversed(rows))):
            for capacity in (0, 1, 2, 9, 512, 4096):
                status, result, error = self.call(_request(observations=selected), capacity)
                self.assertEqual((status, result.artifact_count, result.total_exact_bytes), (0, 0, 0))
                if capacity:
                    self.assertEqual(error.value, expected[:capacity - 1])

    def test_failure_markers_require_zero_size_and_known_kind(self):
        from tests.test_artifact_size_policy_contract import _policy, _observations
        original = _observations(_policy())
        for kind, size in ((2, 1), (3, 1), (4, 1), (5, 1), (6, 1), (0, 0), (7, 0)):
            rows = list(original)
            rows[0] = (rows[0][0], kind, size)
            status, result, error = self.call(_request(observations=rows))
            self.assertEqual((status, result.artifact_count, result.total_exact_bytes), (0, 0, 0))
            self.assertIn(b"not a regular file", error.value)
        rows[0] = (rows[1][0], 3, 0)
        status, result, error = self.call(_request(observations=rows))
        self.assertEqual(status, 0)
        self.assertIn(b"duplicated", error.value)
        self.assertNotIn(b"\n- ", error.value)


if __name__ == "__main__":
    unittest.main()

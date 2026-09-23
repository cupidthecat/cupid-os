import concurrent.futures
import copy
import ctypes
import json
import os
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DRAFT = ROOT / 'toolchain'
sys.path.insert(0, str(ROOT))
from tests.test_artifact_size_policy_contract import _host_compiler
from tools import bootstrap_toolchain as seed

ROLES = ("cupidasm", "cupidc", "cupiddis", "cupidld", "cupidobj", "cupidbuild")


def release():
    return {
        "schema": "cupid.seed-release.v1",
        "source_revision": seed.PROMOTED_SOURCE_REVISION,
        "source_snapshot_sha256": seed.PROMOTED_SOURCE_SNAPSHOT_SHA256,
        "source_input_count": seed.PROMOTED_SOURCE_INPUT_COUNT,
        "parent_source_revision": seed.PROMOTION_PARENT_SOURCE_REVISION,
        "parent_linux_manifest_sha256": seed.PROMOTION_PARENT_LINUX_MANIFEST_SHA256,
        "parent_windows_manifest_sha256": seed.PROMOTION_PARENT_WINDOWS_MANIFEST_SHA256,
        "linux_plan_sha256": seed.PROMOTED_LINUX_PLAN_SHA256,
        "windows_plan_sha256": seed.PROMOTED_WINDOWS_PLAN_SHA256,
        "artifacts": [
            {"name": role, "format": fmt, "size": identities[role][0],
             "sha256": identities[role][1]}
            for fmt, identities in (
                ("elf32", seed.PROMOTED_LINUX_ARTIFACT_IDENTITIES),
                ("pe32", seed.PROMOTED_WINDOWS_ARTIFACT_IDENTITIES))
            for role in ROLES
        ],
    }


class Artifact(ctypes.Structure):
    _fields_ = [("size", ctypes.c_uint64), ("sha256", ctypes.c_char * 65)]


class Release(ctypes.Structure):
    _fields_ = [
        ("source_revision", ctypes.c_char * 41),
        ("source_snapshot_sha256", ctypes.c_char * 65),
        ("source_input_count", ctypes.c_uint32),
        ("parent_source_revision", ctypes.c_char * 41),
        ("parent_linux_manifest_sha256", ctypes.c_char * 65),
        ("parent_windows_manifest_sha256", ctypes.c_char * 65),
        ("linux_plan_sha256", ctypes.c_char * 65),
        ("windows_plan_sha256", ctypes.c_char * 65),
        ("artifacts", (Artifact * 6) * 2),
    ]


def encode(value):
    return json.dumps(value, separators=(",", ":")).encode()


class ReleaseTests(unittest.TestCase):
    records = []
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="cupid-release-")
        cls.addClassCleanup(cls.tmp.cleanup)
        tmp = Path(cls.tmp.name)
        shim = tmp / "shim.c"
        shim.write_text('#include "seed_release.h"\n'
                        '#if defined(_WIN32)\n__declspec(dllexport)\n#endif\n'
                        'int test_parse(const unsigned char *p, size_t n, '
                        'cupid_seed_release_t *r, char *e, size_t c) { '
                        'return cupid_seed_release_parse(p,n,r,e,c); }\n'
                        '#if defined(_WIN32)\n__declspec(dllexport)\n#endif\n'
                        'int test_match(const unsigned char *r, size_t rn, '
                        'const unsigned char *m, size_t mn, unsigned int f, '
                        'char *e, size_t c) { return cupid_seed_release_match_manifest(r,rn,m,mn,f,e,c); }\n')
        library = tmp / ("release.dll" if os.name == "nt" else "release.so")
        command = [_host_compiler(), "-std=c11", "-O2", "-pedantic", "-Wall",
                   "-Wextra", "-Werror", "-shared", "-I", str(DRAFT),
                   "-I", str(ROOT / "toolchain"), "-x", "c", str(shim),
                   str(DRAFT / "seed_release.cc"),
                   str(ROOT / "toolchain/contract_parse_internal.cc"),
                   "-o", str(library)]
        if os.name != "nt":
            command.append("-fPIC")
        subprocess.run(command, check=True, capture_output=True, timeout=60)
        cls.library = ctypes.CDLL(str(library))
        if os.name == "nt":
            cls.addClassCleanup(cls.unload)
        cls.api = cls.library.test_parse
        cls.api.argtypes = [ctypes.c_void_p, ctypes.c_size_t,
                           ctypes.POINTER(Release), ctypes.c_void_p, ctypes.c_size_t]
        cls.api.restype = ctypes.c_int

    @classmethod
    def unload(cls):
        import _ctypes
        _ctypes.FreeLibrary(cls.library._handle)
        cls.library._handle = 0

    def call(self, data, valid=True, capacity=128):
        if isinstance(data, dict):
            data = encode(data)
        incoming = ctypes.create_string_buffer(data)
        before = bytes(incoming)
        result = Release()
        ctypes.memset(ctypes.byref(result), 0xa5, ctypes.sizeof(result))
        error = ctypes.create_string_buffer(b"!" * (capacity + 8))
        status = self.api(incoming, len(data), ctypes.byref(result), error, capacity)
        self.assertEqual(status, int(valid), (data[:120], error.value))
        self.assertEqual(bytes(incoming), before)
        self.assertEqual(bytes(error)[capacity:], b"!" * 8 + b"\0")
        if not valid:
            self.assertEqual(bytes(result), bytes(ctypes.sizeof(result)))
            if capacity > 1:
                self.assertTrue(error.value)
        elif capacity:
            self.assertEqual(error.value, b"")
        if os.environ.get("CUPID_RELEASE_EXPORT"):
            fields = ["1" if valid else "0"]
            if valid:
                for key, _ in Release._fields_[:-1]:
                    value = getattr(result, key)
                    fields.append(value.decode() if isinstance(value, bytes) else str(value))
                for cohort in result.artifacts:
                    for artifact in cohort:
                        fields.extend((str(artifact.size), artifact.sha256.decode()))
            self.records.append((data, " ".join(fields) + "\n"))
        return result, error

    def test_current_release_preserves_every_pin(self):
        expected = release()
        actual, _ = self.call(expected)
        for key, value in expected.items():
            if key in ("schema", "artifacts"):
                continue
            self.assertEqual(getattr(actual, key), value.encode() if isinstance(value, str) else value)
        for row in expected["artifacts"]:
            artifact = actual.artifacts[("elf32", "pe32").index(row["format"])][ROLES.index(row["name"])]
            self.assertEqual((artifact.size, artifact.sha256), (row["size"], row["sha256"].encode()))

    def test_all_key_and_row_order_is_semantic(self):
        expected, _ = self.call(release())
        value = dict(reversed(list(release().items())))
        value["artifacts"] = [dict(reversed(list(row.items()))) for row in reversed(value["artifacts"])]
        for data in (encode(value), json.dumps(value, indent=3).encode(),
                     encode(value).replace(b'"schema"', b'"sch\\u0065ma"')):
            actual, _ = self.call(data)
            self.assertEqual(bytes(actual), bytes(expected))

    def test_missing_unknown_duplicate_top_fields(self):
        for key in release():
            value = release()
            del value[key]
            self.call(value, False)
            data = encode(release())
            duplicate = encode({key: release()[key]})[1:-1]
            self.call(data[:-1] + b"," + duplicate + b"}", False)
        value = release()
        value["extra"] = None
        self.call(value, False)
        self.call(encode(release())[:-1] + b',"sch\\u0065ma":"cupid.seed-release.v1"}', False)

    def test_missing_unknown_duplicate_artifact_fields(self):
        for key in release()["artifacts"][0]:
            value = release()
            del value["artifacts"][0][key]
            self.call(value, False)
            data = encode(release())
            row = encode(release()["artifacts"][0])
            duplicate = encode({key: release()["artifacts"][0][key]})[1:-1]
            self.call(data.replace(row, row[:-1] + b"," + duplicate + b"}", 1), False)
        value = release()
        value["artifacts"][0]["extra"] = 1
        self.call(value, False)

    def test_hex_fields_reject_wrong_shape_type_and_encoding(self):
        for key, original in release().items():
            if key in ("schema", "source_input_count", "artifacts"):
                continue
            for invalid in ("", original[:-1], original + "a", "G" * len(original),
                            original.upper(), None, 7, [], {}, "\0" * len(original)):
                value = release()
                value[key] = invalid
                self.call(value, False)
        for invalid in ("x" * 64, "A" * 64, "a" * 63, "a" * 65, False):
            value = release()
            value["artifacts"][0]["sha256"] = invalid
            self.call(value, False)

    def test_integer_bounds_and_exact_types(self):
        for key, maximum in (("source_input_count", 2**32 - 1), ("size", 64 * 1024 * 1024)):
            for number in (0, -1, True, None, "59", 1.5, 1e30, maximum + 1, 2**64):
                value = release()
                target = value if key == "source_input_count" else value["artifacts"][0]
                target[key] = number
                self.call(value, False)
            for number in (1, maximum):
                value = release()
                target = value if key == "source_input_count" else value["artifacts"][0]
                target[key] = number
                self.call(value)
        data = encode(release())
        for token in (b"059", b"59.0", b"59e0", b"+59", b"-59"):
            self.call(data.replace(b'"source_input_count":59', b'"source_input_count":' + token), False)

    def test_exact_two_cohorts(self):
        for index in range(12):
            value = release()
            del value["artifacts"][index]
            self.call(value, False)
            value = release()
            value["artifacts"][index] = copy.deepcopy(value["artifacts"][(index + 1) % 12])
            self.call(value, False)
        value = release()
        value["artifacts"].append(copy.deepcopy(value["artifacts"][0]))
        self.call(value, False)
        for key, wrong in (("name", "cupidunknown"), ("format", "ELF32"),
                           ("name", "cupidasm.elf"), ("format", "elf64")):
            value = release()
            value["artifacts"][0][key] = wrong
            self.call(value, False)

    def test_syntax_truncation_and_input_bound(self):
        data = encode(release())
        for end in range(len(data)):
            self.call(data[:end], False, 0)
        for suffix in (b"x", b"{}", b"\0", b",", b"\xff"):
            self.call(data + suffix, False)
        for invalid in (b"[]", b"null", b"{}", data[:-1] + b",}",
                        data.replace(b'"artifacts":[', b'"artifacts":[,', 1),
                        b" " * 65537, b"\xef\xbb\xbf" + data,
                        data.replace(b"cupid.seed", b"cupid.\xffseed", 1)):
            self.call(invalid, False)
        self.call(data + b" " * (65536 - len(data)))
        self.call(data + b" " * (65537 - len(data)), False)

    def test_schema_and_container_types(self):
        for value in ("cupid.seed-release.v2", "", None, 1, [], {}):
            data = release()
            data["schema"] = value
            self.call(data, False)
        for value in (None, {}, "", 12):
            data = release()
            data["artifacts"] = value
            self.call(data, False)

    def test_diagnostics_null_arguments_and_recovery(self):
        for capacity in (0, 1, 2, 8, 128):
            self.call(b"bad", False, capacity)
            self.call(release(), True, capacity)
        result = Release()
        ctypes.memset(ctypes.byref(result), 0xa5, ctypes.sizeof(result))
        self.assertEqual(self.api(None, 1, ctypes.byref(result), None, 0), 0)
        self.assertEqual(bytes(result), bytes(ctypes.sizeof(result)))
        data = ctypes.create_string_buffer(encode(release()))
        self.assertEqual(self.api(data, len(data) - 1, None, None, 0), 0)
        self.assertEqual(self.api(data, len(data) - 1, ctypes.byref(result), None, 1), 0)
        self.assertEqual(self.api(data, len(data) - 1, ctypes.byref(result), None, 0), 1)

    def test_concurrent_calls_and_changed_generation(self):
        def run(index):
            data = release()
            data["source_revision"] = f"{index:040x}"
            data["artifacts"][11]["sha256"] = f"{index:064x}"
            actual, _ = self.call(data)
            self.assertEqual(actual.source_revision, data["source_revision"].encode())
            self.assertEqual(actual.artifacts[1][5].sha256, data["artifacts"][11]["sha256"].encode())
            self.call(b"bad", False)
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            list(pool.map(run, range(128)))


if __name__ == "__main__":
    outcome = unittest.main(exit=False)
    if outcome.result.wasSuccessful() and os.environ.get("CUPID_RELEASE_EXPORT"):
        destination = Path(os.environ["CUPID_RELEASE_EXPORT"])
        with destination.with_suffix(".bin").open("xb") as stream:
            for payload, _ in ReleaseTests.records:
                stream.write(struct.pack("<I", len(payload)))
                stream.write(payload)
        destination.with_suffix(".txt").write_text("".join(row[1] for row in ReleaseTests.records), encoding="ascii")
        print(f"Exported {len(ReleaseTests.records)} checked-caller cases")
    sys.exit(not outcome.result.wasSuccessful())

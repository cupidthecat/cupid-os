import concurrent.futures
import copy
import ctypes
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from tests.test_seed_release import DRAFT, ROOT, encode, _host_compiler
from tests.test_seed_release_match import manifest
from tools import bootstrap_toolchain as seed


def historical_manifest(fmt):
    """Construct the earlier structural contract without changing installed seeds."""
    value = manifest(fmt)
    value["provenance"]["source_input_count"] = 59
    if fmt == 1:
        removed = {"seed_manifest", "seed_release", "contract_parse_internal"}
        plan = value["build_plan"]
        plan["sources"] = [row for row in plan["sources"] if row["name"] not in removed]
        plan["links"]["cupidbuild"] = [name for name in plan["links"]["cupidbuild"]
                                      if name not in removed]
        value["build_plan_sha256"] = seed._build_plan_sha256(plan)
        assert value["build_plan_sha256"] == "52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd"
    else:
        value["provenance"]["linux_candidate_build_plan_sha256"] = "52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd"
        value["provenance"]["native_build_plan_sha256"] = "98e09aab876a9fa37ec07c38a0a57a014549a14c0ab10c740b3f80ede9d65669"
    return value


def candidate_manifest(fmt):
    value = manifest(fmt)
    value["provenance"]["source_input_count"] = 66
    plan = seed._candidate_build_plan(manifest(1)["build_plan"])
    linux_digest = seed._build_plan_sha256(plan)
    windows_digest = seed._build_plan_sha256(seed._windows_build_plan(plan))
    assert linux_digest == "fc1c7634d4cb6a9106c523fe7c5c82f38e2b8e3eb3b3dbce9166e93daa4116fe"
    assert windows_digest == "70158fd9780990ec0cd0ed1c4da1af9f22f8acbcb483324693fd46c2362177b9"
    if fmt == 1:
        value["build_plan"] = plan
        value["build_plan_sha256"] = linux_digest
    else:
        value["provenance"]["linux_candidate_build_plan_sha256"] = linux_digest
        value["provenance"]["native_build_plan_sha256"] = windows_digest
    return value


class Artifact(ctypes.Structure):
    _fields_ = [("file", ctypes.c_char * 32), ("sha256", ctypes.c_char * 65), ("size", ctypes.c_uint32)]


class Result(ctypes.Structure):
    _fields_ = [("artifact_count", ctypes.c_uint32), ("current_windows_plan", ctypes.c_uint32),
               ("artifacts", Artifact * 6)]


def leaves(value, path=()):
    if isinstance(value, dict):
        for key, child in value.items():
            yield from leaves(child, path + (key,))
    elif isinstance(value, list):
        for index, child in enumerate(value):
            yield from leaves(child, path + (index,))
    else:
        yield path, value


def changed(value):
    if isinstance(value, bool): return not value
    if isinstance(value, int): return value + 1
    return "wrong"


class ManifestTests(unittest.TestCase):
    records = []

    def test_utf8_promotion_accepts_exact_conditional_assembly_parent(self):
        revision = "e4f2ed652e756b1abb375ec061923f5259799e01"
        linux = "da26556401dd20d039ed1175f3bf857c4c8bebd50fb52b3bd75a06b95fdf41ed"
        windows = "c715ce354c28b97c6b9c4e5702c98d368d07dff9e52bc2f0deb71ab3194d2395"
        for fmt in (1, 2):
            value = candidate_manifest(fmt)
            value["provenance"]["source_input_count"] = 73
            if fmt == 1:
                parents = {"parent_seed_manifest_sha256": linux,
                           "parent_seed_source_revision": revision}
            else:
                value["provenance"]["native_build_plan_sha256"] = seed._build_plan_sha256(
                    seed._windows_build_plan(candidate_manifest(1)["build_plan"], utf8=True))
                parents = {"parent_execution_seed_manifest_sha256": windows,
                           "parent_execution_seed_source_revision": revision,
                           "parent_plan_seed_manifest_sha256": linux,
                           "parent_plan_seed_source_revision": revision}
            previous = {key: value["provenance"][key] for key in parents}
            value["provenance"].update(parents)
            self.check(value, fmt, expected=(6, 2 if fmt == 2 else 0))
            for key, expected in parents.items():
                for replacement in ("0" * len(expected), previous[key]):
                    altered = copy.deepcopy(value)
                    altered["provenance"][key] = replacement
                    self.check(altered, fmt, False)
                    self.check(value, fmt, expected=(6, 2 if fmt == 2 else 0))

    def test_windows_behavior_seed_has_consistent_candidate_provenance(self):
        with tempfile.TemporaryDirectory(prefix="cupid-behavior-manifest-") as temporary:
            frozen = seed.freeze_seed_inputs(
                ROOT / "bootstrap/seeds/i386-windows/manifest.json",
                Path(temporary) / "seed",
            )
            plan = seed._candidate_build_plan(manifest(1)["build_plan"])
            digest = seed._build_plan_sha256(seed._windows_build_plan(plan, utf8=True))
            snapshot = seed.capture_source_snapshot(ROOT, plan, windows_utf8=True)
            retargeted = seed._retarget_native_windows_behavior_seed(
                frozen, digest, plan, snapshot, utf8=True)
            self.check(retargeted.manifest, 2, expected=(6, 2))
            for field in ("source_input_count", "linux_candidate_build_plan_sha256"):
                altered = copy.deepcopy(retargeted.manifest)
                altered["provenance"][field] = historical_manifest(2)["provenance"][field]
                self.check(altered, 2, False)

    def test_candidate_plan_keeps_its_source_count_and_complete_closure(self):
        for fmt in (1, 2):
            self.check(candidate_manifest(fmt), fmt)
            for count in (50, 59, 61, 65, 67):
                value = candidate_manifest(fmt)
                value["provenance"]["source_input_count"] = count
                self.check(value, fmt, False)
            value = historical_manifest(fmt)
            value["provenance"]["source_input_count"] = 66
            self.check(value, fmt, False)

    def test_utf8_generation_binds_source_count_and_exact_plan_pair(self):
        for fmt in (1, 2):
            value = candidate_manifest(fmt)
            value["provenance"]["source_input_count"] = 73
            if fmt == 2:
                linux = candidate_manifest(1)["build_plan"]
                value["provenance"]["native_build_plan_sha256"] = seed._build_plan_sha256(
                    seed._windows_build_plan(linux, utf8=True))
            self.check(value, fmt, expected=(6, 2 if fmt == 2 else 0))
            for count in (68, 69, 70, 71, 72, 74, True, 73.0):
                altered = copy.deepcopy(value)
                altered["provenance"]["source_input_count"] = count
                self.check(altered, fmt, False)
            if fmt == 2:
                altered = copy.deepcopy(value)
                altered["provenance"]["source_input_count"] = 66
                self.check(altered, fmt, False)
                for key in ("native_build_plan_sha256", "linux_candidate_build_plan_sha256"):
                    altered = copy.deepcopy(value)
                    altered["provenance"][key] = historical_manifest(2)["provenance"][key]
                    self.check(altered, fmt, False)
                altered = candidate_manifest(2)
                altered["provenance"]["source_input_count"] = 73
                self.check(altered, fmt, False)

    def test_candidate_plan_rejects_each_changed_source_and_link(self):
        for section in ("sources", "links"):
            value = candidate_manifest(1)
            for path, old in leaves(value["build_plan"][section]):
                altered = copy.deepcopy(value)
                target = altered["build_plan"][section]
                for part in path[:-1]:
                    target = target[part]
                target[path[-1]] = changed(old)
                self.check(altered, 1, False)
        for key in ("linux_candidate_build_plan_sha256", "native_build_plan_sha256"):
            value = candidate_manifest(2)
            value["provenance"][key] = historical_manifest(2)["provenance"][key]
            self.check(value, 2, False)

    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory(prefix="cupid-manifest-api-")
        cls.addClassCleanup(cls.tmp.cleanup)
        tmp = Path(cls.tmp.name)
        shim = tmp / "shim.c"
        shim.write_text('#include "seed_manifest.h"\n'
                        '#if defined(_WIN32)\n__declspec(dllexport)\n#endif\n'
                        'int test_manifest(const unsigned char *p, size_t n, unsigned int f, '
                        'cupid_seed_manifest_result_t *r, char *e, size_t c) { '
                        'return cupid_seed_manifest_validate(p,n,f,r,e,c); }\n'
                        '#if defined(_WIN32)\n__declspec(dllexport)\n#endif\n'
                        'int test_pair(const unsigned char *r, size_t rn, '
                        'const unsigned char *l, size_t ln, const unsigned char *w, size_t wn, '
                        'char *e, size_t c) { return cupid_seed_pair_validate(r,rn,l,ln,w,wn,e,c); }\n')
        library = tmp / ("manifest.dll" if os.name == "nt" else "manifest.so")
        command = [_host_compiler(), "-std=c11", "-O2", "-pedantic", "-Wall", "-Wextra", "-Werror",
                   "-shared", "-I", str(DRAFT), "-I", str(ROOT / "toolchain"), "-x", "c", str(shim),
                   str(DRAFT / "seed_manifest.cc"), str(ROOT / "toolchain/contract_parse_internal.cc"),
                   str(DRAFT / "seed_release.cc"), str(ROOT / "toolchain/cupidbuild_host.cc"),
                   str(ROOT / "toolchain/path_encoding.cc"),
                   "-o", str(library)]
        if os.name != "nt": command.append("-fPIC")
        else: command.extend(["-D_CRT_SECURE_NO_WARNINGS", "-lntdll"])
        built = subprocess.run(command, capture_output=True, text=True, timeout=60)
        if built.returncode: raise AssertionError(built.stdout + built.stderr)
        cls.library = ctypes.CDLL(str(library))
        if os.name == "nt": cls.addClassCleanup(cls.unload)
        cls.api = cls.library.test_manifest
        cls.api.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint, ctypes.POINTER(Result),
                           ctypes.c_void_p, ctypes.c_size_t]
        cls.api.restype = ctypes.c_int

    @classmethod
    def unload(cls):
        import _ctypes
        _ctypes.FreeLibrary(cls.library._handle)
        cls.library._handle = 0

    def check(self, value, fmt, accepted=True, capacity=128, expected=None):
        data = encode(value) if isinstance(value, dict) else value
        incoming = ctypes.create_string_buffer(data)
        before = bytes(incoming)
        result = Result(99, 99)
        ctypes.memset(ctypes.byref(result), 0xa5, ctypes.sizeof(result))
        error = ctypes.create_string_buffer(b"!" * (capacity + 8))
        status = self.api(incoming, len(data), fmt, ctypes.byref(result), error, capacity)
        self.assertEqual(status, int(accepted), (fmt, error.value, data[:80]))
        self.assertEqual(bytes(incoming), before)
        self.assertEqual(bytes(error)[capacity:], b"!" * 8 + b"\0")
        if accepted:
            expected = expected or (6, int(fmt == 2))
            self.assertEqual((result.artifact_count, result.current_windows_plan), expected)
            decoded = json.loads(data)
            roles = ("cupidasm", "cupidc", "cupiddis", "cupidld", "cupidobj", "cupidbuild")
            summary = ["1", str(expected[0]), str(expected[1])]
            for index, role in enumerate(roles):
                actual = result.artifacts[index]
                if index >= result.artifact_count:
                    self.assertEqual(bytes(actual), bytes(ctypes.sizeof(actual)))
                    continue
                row = next(row for row in decoded["artifacts"] if row["name"] == role)
                self.assertEqual((actual.file.decode(), actual.size, actual.sha256.decode()),
                                 (row["file"], row["size"], row["sha256"]))
                summary.extend((actual.file.decode(), str(actual.size), actual.sha256.decode()))
            if capacity: self.assertEqual(error.value, b"")
        else:
            self.assertEqual(bytes(result), bytes(ctypes.sizeof(result)))
            summary = ["0"]
            if capacity > 1: self.assertTrue(error.value)
        if os.environ.get("CUPID_MANIFEST_EXPORT"):
            self.records.append((data, fmt, " ".join(summary) + "\n"))

    def test_legacy_five_tool_manifests_and_previous_windows_plan(self):
        from tools import bootstrap_toolchain as seed
        for fmt in (1, 2):
            value = historical_manifest(fmt)
            value["schema"] = value["schema"].replace(".v2", ".v1")
            value["artifacts"] = [row for row in value["artifacts"] if row["name"] != "cupidbuild"]
            previous = value["provenance"]
            provenance = {key: previous[key] for key in ("fixed_point_command", "fixed_point_result", "producer_lineage")}
            provenance.update(source_revision=seed.SEED_SOURCE_REVISION,
                              source_snapshot_sha256=seed.SEED_SOURCE_SNAPSHOT_SHA256,
                              source_input_count=50)
            if fmt == 1:
                provenance["seed_generation"] = "stage-four"
                value["build_plan"]["sources"] = value["build_plan"]["sources"][:-3]
                del value["build_plan"]["links"]["cupidbuild"]
                value["build_plan_sha256"] = "59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc"
            else:
                provenance.update(artifact_generation="paired-stage-four-native-windows",
                    parent_seed_manifest_sha256="b6e34a2e18dd18aba91c6358116eafde39953566efeadb224575ac8c13ab2c1b",
                    parent_seed_source_revision=seed.SEED_SOURCE_REVISION)
            value["provenance"] = provenance
            self.check(value, fmt, expected=(5, 0))
            self.check(value, 3 - fmt, False)
            value["provenance"]["source_input_count"] = 59
            self.check(value, fmt, False)
        value = historical_manifest(2)
        value["provenance"]["native_build_plan_sha256"] = "f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c995086bfe723cff14"
        self.check(value, 2, expected=(6, 0))

    def test_artifact_size_bound_is_independent_of_host_word_size(self):
        for fmt in (1, 2):
            for size in (1, 67108864, 67108865, 2**32 - 1, 2**32, 2**64 - 1):
                value = manifest(fmt)
                value["artifacts"][0]["size"] = size
                self.check(value, fmt, size <= 67108864)

    def test_null_arguments_clear_result_and_allow_optional_diagnostics(self):
        result = Result(99, 99)
        raw = encode(manifest(1))
        data = ctypes.create_string_buffer(raw)
        self.assertEqual(self.api(None, 1, 1, ctypes.byref(result), None, 0), 0)
        self.assertEqual(bytes(result), bytes(ctypes.sizeof(result)))
        self.assertEqual(self.api(data, len(raw), 1, None, None, 0), 0)
        self.assertEqual(self.api(data, len(raw), 1, ctypes.byref(result), None, 1), 0)
        self.assertEqual(self.api(data, len(raw), 1, ctypes.byref(result), None, 0), 1)

    def test_both_formats_and_reordering(self):
        for fmt in (1, 2):
            value = manifest(fmt)
            self.check(value, fmt)
            self.check(value, 3 - fmt, False)
            value = dict(reversed(list(value.items())))
            value["artifacts"].reverse()
            self.check(value, fmt)
            self.check(json.dumps(value, indent=3).encode(), fmt)

    def test_escaped_keys_and_every_string_value(self):
        for fmt in (1, 2):
            data = encode(manifest(fmt))
            escaped = []
            # All current manifest strings are ASCII and contain no escaped quotes.
            parts = data.split(b'"')
            for index, part in enumerate(parts):
                if index % 2:
                    part = b"".join(f"\\u{byte:04x}".encode() for byte in part)
                escaped.append(part)
            self.check(b'"'.join(escaped), fmt)
            for old, new in ((b'"schema"', b'"sch\\u0065ma"'),
                             (b"cupidasm", b"cupid\\u0061sm"),
                             (b"/toolchain", b"\\/toolchain")):
                self.check(data.replace(old, new), fmt)

    def test_every_fixed_target_lineage_and_plan_leaf(self):
        for fmt in (1, 2):
            candidate = manifest(fmt)
            locations = [("target",), ("provenance", "producer_lineage")]
            if fmt == 1: locations.append(("build_plan",))
            for base in locations:
                subtree = candidate
                for key in base: subtree = subtree[key]
                for path, value in leaves(subtree):
                    altered = copy.deepcopy(candidate)
                    destination = altered
                    full = base + path
                    for key in full[:-1]: destination = destination[key]
                    destination[full[-1]] = changed(value)
                    with self.subTest(fmt=fmt, path=full): self.check(altered, fmt, False)
            altered = copy.deepcopy(candidate)
            altered["provenance"]["fixed_point_result"] = "fail"
            self.check(altered, fmt, False)

    def test_plan_array_membership_and_order(self):
        for key in ("sources", "include_arguments", "producer_tools"):
            for operation in ("drop", "extra", "reverse"):
                value = manifest(1)
                rows = value["build_plan"][key]
                if operation == "drop": rows.pop()
                elif operation == "extra": rows.append(copy.deepcopy(rows[0]))
                else: rows.reverse()
                self.check(value, 1, False)
        for tool in manifest(1)["build_plan"]["links"]:
            value = manifest(1)
            value["build_plan"]["links"][tool].reverse()
            self.check(value, 1, False)

    def test_missing_extra_duplicate_object_fields(self):
        def objects(value, path=()):
            if isinstance(value, dict):
                yield path, value
                for key, child in value.items(): yield from objects(child, path + (key,))
            elif isinstance(value, list):
                for index, child in enumerate(value): yield from objects(child, path + (index,))
        for fmt in (1, 2):
            baseline = manifest(fmt)
            for location, target in objects(baseline):
                for key in target:
                    candidate = copy.deepcopy(baseline)
                    altered = candidate
                    for part in location: altered = altered[part]
                    del altered[key]
                    self.check(candidate, fmt, False)
                raw = encode(baseline)
                obj = encode(target)
                key = next(iter(target))
                duplicate = encode({key: target[key]})[1:-1]
                self.check(raw.replace(obj, obj[:-1] + b"," + duplicate + b"}", 1), fmt, False)
                self.check(raw.replace(obj, obj[:-1] + b',"unknown":0}', 1), fmt, False)
            raw = encode(baseline)
            self.check(raw[:-1] + b',"sch\\u0065ma":"' + baseline["schema"].encode() + b'"}', fmt, False)

    def test_source_admission_and_parent_binding(self):
        for fmt in (1, 2):
            for count in (58, 60, 62, True, 59.0):
                value = manifest(fmt)
                value["provenance"]["source_input_count"] = count
                self.check(value, fmt, False)
            value = historical_manifest(fmt)
            value["provenance"]["source_input_count"] = 61
            value["provenance"]["source_revision"] = "f" * 40
            self.check(value, fmt)
            for key, val in value["provenance"].items():
                if key.startswith("parent_"):
                    bad = copy.deepcopy(value)
                    bad["provenance"][key] = "1" * len(val)
                    self.check(bad, fmt, False)

    def test_artifact_role_boolean_digest_and_exact_inventory(self):
        for fmt in (1, 2):
            for index in range(6):
                for key, invalid in (("file", "../escape"), ("name", "wrong"),
                                     ("producer", 1), ("sha256", "G" * 64), ("size", 0)):
                    value = manifest(fmt)
                    value["artifacts"][index][key] = invalid
                    self.check(value, fmt, False)
            value = manifest(fmt)
            value["artifacts"][5] = copy.deepcopy(value["artifacts"][0])
            self.check(value, fmt, False)

    def test_truncation_syntax_limits_and_diagnostics(self):
        for fmt in (1, 2):
            raw = encode(manifest(fmt))
            for end in sorted({0, 1, len(raw) - 1, *range(0, len(raw), 41)}):
                self.check(raw[:end], fmt, False, 0)
            for extra in (b"x", b"{}", b"\0", b"\xff"):
                self.check(raw + extra, fmt, False)
            for replacement in (b"\\ud800", b"\\udfff", b"\\u0000", b"\xff"):
                self.check(raw.replace(b"cupidasm", replacement, 1), fmt, False)
            for capacity in (0, 1, 2, 8, 128):
                self.check(b"bad", fmt, False, capacity)
                self.check(raw, fmt, True, capacity)
            self.check(raw + b" " * (1048576 - len(raw)), fmt)
            self.check(raw + b" " * (1048577 - len(raw)), fmt, False)
        self.check(manifest(1), 0, False)
        self.check(manifest(1), 3, False)

    def test_concurrent_formats_and_results(self):
        def run(index):
            fmt = index % 2 + 1
            self.check(manifest(fmt), fmt)
            self.check(manifest(fmt), 3 - fmt, False)
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            list(pool.map(run, range(64)))


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(ManifestTests)
    outcome = unittest.TextTestRunner().run(suite)
    if outcome.wasSuccessful() and os.environ.get("CUPID_MANIFEST_EXPORT"):
        destination = Path(os.environ["CUPID_MANIFEST_EXPORT"])
        with destination.with_suffix(".bin").open("xb") as stream:
            for data, fmt, _ in ManifestTests.records:
                stream.write(struct.pack("<II", len(data), fmt))
                stream.write(data)
        with destination.with_suffix(".txt").open("x") as stream:
            stream.write("".join(summary for _, _, summary in ManifestTests.records))
        print(f"Exported {len(ManifestTests.records)} structural manifest cases")
    sys.exit(not outcome.wasSuccessful())

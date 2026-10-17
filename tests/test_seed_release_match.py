import concurrent.futures
import copy
import ctypes
import hashlib
import json
import os
import struct
import sys
import unittest
from pathlib import Path
from tests.test_seed_release import ROOT, DRAFT, release, encode
from tests import test_seed_release as release_tests


def manifest(fmt):
    directory = "i386-linux" if fmt == 1 else "i386-windows"
    return json.loads((ROOT / "bootstrap/seeds" / directory / "manifest.json").read_text())


class MatchTests(unittest.TestCase):
    records = []
    unload = classmethod(release_tests.ReleaseTests.unload.__func__)

    @classmethod
    def setUpClass(cls):
        release_tests.ReleaseTests.setUpClass.__func__(cls)
        cls.match = cls.library.test_match
        cls.match.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p,
                             ctypes.c_size_t, ctypes.c_uint, ctypes.c_void_p, ctypes.c_size_t]
        cls.match.restype = ctypes.c_int

    def check(self, record, candidate, fmt, accepted=True, capacity=128):
        raw_record = encode(record) if isinstance(record, dict) else record
        raw_manifest = encode(candidate) if isinstance(candidate, dict) else candidate
        record_bytes = ctypes.create_string_buffer(raw_record)
        manifest_bytes = ctypes.create_string_buffer(raw_manifest)
        before_record, before_manifest = bytes(record_bytes), bytes(manifest_bytes)
        error = ctypes.create_string_buffer(b"!" * (capacity + 8))
        result = self.match(record_bytes, len(raw_record), manifest_bytes, len(raw_manifest),
                            fmt, error, capacity)
        self.assertEqual(result, int(accepted), (fmt, error.value, raw_manifest[:80]))
        self.assertEqual(bytes(record_bytes), before_record)
        self.assertEqual(bytes(manifest_bytes), before_manifest)
        self.assertEqual(bytes(error)[capacity:], b"!" * 8 + b"\0")
        if capacity and accepted:
            self.assertEqual(error.value, b"")
        if capacity > 1 and not accepted:
            self.assertTrue(error.value)
        if os.environ.get("CUPID_RELEASE_MATCH_EXPORT"):
            self.records.append((raw_record, raw_manifest, fmt, accepted))

    def test_current_pair_and_semantic_reordering(self):
        for fmt in (1, 2):
            value = manifest(fmt)
            self.check(release(), value, fmt)
            value = dict(reversed(list(value.items())))
            value["provenance"] = dict(reversed(list(value["provenance"].items())))
            value["artifacts"] = [dict(reversed(list(row.items()))) for row in reversed(value["artifacts"])]
            self.check(release(), value, fmt)
            self.check(release(), json.dumps(value, indent=2).encode(), fmt)
            self.check(release(), encode(value).replace(b'"schema"', b'"sch\\u0065ma"'), fmt)
            self.check(release(), value, 3 - fmt, False)

    def test_every_artifact_identity_is_pinned(self):
        for fmt in (1, 2):
            for index in range(6):
                for key, value in (("size", 1), ("sha256", "1" * 64),
                                   ("file", "other.elf"), ("producer", None),
                                   ("name", "other")):
                    candidate = manifest(fmt)
                    candidate["artifacts"][index][key] = value
                    self.check(release(), candidate, fmt, False)
                candidate = manifest(fmt)
                candidate["artifacts"][index]["producer"] = not candidate["artifacts"][index]["producer"]
                self.check(release(), candidate, fmt, False)

    def test_changed_tool_with_matching_manifest_is_rejected(self):
        for fmt in (1, 2):
            candidate = manifest(fmt)
            directory = ROOT / "bootstrap/seeds" / ("i386-linux" if fmt == 1 else "i386-windows")
            for index in range(6):
                changed = copy.deepcopy(candidate)
                artifact = changed["artifacts"][index]
                payload = bytearray((directory / artifact["file"]).read_bytes())
                payload[-1] ^= 1
                artifact["sha256"] = hashlib.sha256(payload).hexdigest()
                self.assertEqual(len(payload), artifact["size"])
                self.check(release(), changed, fmt, False)

    def test_every_provenance_pin_is_checked(self):
        fields = {
            1: ("source_revision", "source_snapshot_sha256", "source_input_count",
                "parent_seed_manifest_sha256", "parent_seed_source_revision"),
            2: ("source_revision", "source_snapshot_sha256", "source_input_count",
                "parent_execution_seed_manifest_sha256", "parent_execution_seed_source_revision",
                "parent_plan_seed_manifest_sha256", "parent_plan_seed_source_revision",
                "linux_candidate_build_plan_sha256", "native_build_plan_sha256"),
        }
        for fmt, names in fields.items():
            for key in names:
                candidate = manifest(fmt)
                old = candidate["provenance"][key]
                candidate["provenance"][key] = old + 1 if isinstance(old, int) else "f" * len(old)
                self.check(release(), candidate, fmt, False)
            for key in ("artifact_generation", "fixed_point_command", "fixed_point_result"):
                candidate = manifest(fmt)
                candidate["provenance"][key] = "wrong"
                self.check(release(), candidate, fmt, False)
        candidate = manifest(1)
        candidate["build_plan_sha256"] = "a" * 64
        self.check(release(), candidate, 1, False)

    def test_release_record_is_an_input_not_an_embedded_generation(self):
        for fmt in (1, 2):
            record = release()
            candidate = manifest(fmt)
            record["source_revision"] = candidate["provenance"]["source_revision"] = "e" * 40
            record["source_snapshot_sha256"] = candidate["provenance"]["source_snapshot_sha256"] = "d" * 64
            record["source_input_count"] = candidate["provenance"]["source_input_count"] = 61
            self.check(record, candidate, fmt)
            self.check(release(), candidate, fmt, False)
            row = candidate["artifacts"][5]
            row["sha256"] = "c" * 64
            for pin in record["artifacts"]:
                if pin["name"] == row["name"] and pin["format"] == ("elf32" if fmt == 1 else "pe32"):
                    pin["sha256"] = row["sha256"]
            self.check(record, candidate, fmt)

    def test_exact_fields_in_each_owned_object(self):
        for fmt in (1, 2):
            for location in ((), ("provenance",), ("artifacts", 0)):
                base = manifest(fmt)
                target = base
                for part in location:
                    target = target[part]
                for key in list(target):
                    candidate = copy.deepcopy(base)
                    modified = candidate
                    for part in location:
                        modified = modified[part]
                    del modified[key]
                    self.check(release(), candidate, fmt, False)
                    raw = encode(base)
                    object_bytes = encode(target)
                    repeated = encode({key: target[key]})[1:-1]
                    self.check(release(), raw.replace(object_bytes, object_bytes[:-1] + b"," + repeated + b"}", 1), fmt, False)
                target["unknown"] = 0
                self.check(release(), base, fmt, False)
            for count in (0, 5, 7):
                candidate = manifest(fmt)
                candidate["artifacts"] = (candidate["artifacts"] * 2)[:count]
                self.check(release(), candidate, fmt, False)
            candidate = manifest(fmt)
            candidate["artifacts"][5] = copy.deepcopy(candidate["artifacts"][0])
            self.check(release(), candidate, fmt, False)

    def test_bad_types_truncation_and_trailing_bytes(self):
        for fmt in (1, 2):
            raw = encode(manifest(fmt))
            for end in sorted(set([0, 1, len(raw) - 1, *range(0, len(raw), 29)])):
                self.check(release(), raw[:end], fmt, False, 0)
            for extra in (b"x", b"{}", b"\0", b"\xff"):
                self.check(release(), raw + extra, fmt, False)
            for key in ("source_revision", "source_snapshot_sha256", "source_input_count"):
                for value in (None, True, [], {}):
                    candidate = manifest(fmt)
                    candidate["provenance"][key] = value
                    self.check(release(), candidate, fmt, False)
            for key in ("target", "artifacts", "provenance"):
                candidate = manifest(fmt)
                candidate[key] = None
                self.check(release(), candidate, fmt, False)
            self.check(b"bad", raw, fmt, False)
            self.check(release(), raw[:-1] + b",}", fmt, False)
            self.check(release(), raw.replace(b'"schema"', b'"sch\\u0065ma":"wrong","schema"', 1), fmt, False)

    def test_bounds_null_diagnostics_and_recovery(self):
        raw = encode(manifest(1))
        for fmt in (0, 3, 0xffffffff):
            self.check(release(), raw, fmt, False)
        self.check(release(), raw + b" " * (1048576 - len(raw)), 1)
        self.check(release(), raw + b" " * (1048577 - len(raw)), 1, False)
        for capacity in (0, 1, 2, 8, 128):
            self.check(release(), b"bad", 1, False, capacity)
            self.check(release(), raw, 1, True, capacity)
        r = ctypes.create_string_buffer(encode(release()))
        m = ctypes.create_string_buffer(raw)
        self.assertEqual(self.match(r, len(r) - 1, None, 1, 1, None, 0), 0)
        self.assertEqual(self.match(None, 1, m, len(raw), 1, None, 0), 0)
        self.assertEqual(self.match(r, len(r) - 1, m, len(raw), 1, None, 1), 0)
        self.assertEqual(self.match(r, len(r) - 1, m, len(raw), 1, None, 0), 1)

    def test_non_release_semantics_remain_separate_checks(self):
        # This API supplements the complete manifest/plan and paired-digest validators.
        for fmt in (1, 2):
            candidate = manifest(fmt)
            candidate["target"] = {}
            candidate["provenance"]["producer_lineage"] = {}
            if fmt == 1:
                candidate["build_plan"] = {}
            else:
                candidate["provenance"]["plan_seed_manifest_sha256"] = "f" * 64
            self.check(release(), candidate, fmt)

    def test_concurrent_matchers_keep_independent_pins(self):
        def run(index):
            record = release()
            fmt = index % 2 + 1
            candidate = manifest(fmt)
            record["source_revision"] = candidate["provenance"]["source_revision"] = f"{index:040x}"
            self.check(record, candidate, fmt)
            self.check(release(), candidate, fmt, False)
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            list(pool.map(run, range(96)))


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(MatchTests)
    outcome = unittest.TextTestRunner().run(suite)
    if outcome.wasSuccessful() and os.environ.get("CUPID_RELEASE_MATCH_EXPORT"):
        destination = Path(os.environ["CUPID_RELEASE_MATCH_EXPORT"])
        with destination.with_suffix(".bin").open("xb") as stream:
            for record, candidate, fmt, _ in MatchTests.records:
                stream.write(struct.pack("<III", len(record), len(candidate), fmt))
                stream.write(record)
                stream.write(candidate)
        with destination.with_suffix(".txt").open("x") as stream:
            stream.write("".join("1\n" if row[3] else "0\n" for row in MatchTests.records))
        print(f"Exported {len(MatchTests.records)} release matching cases")
    sys.exit(not outcome.wasSuccessful())

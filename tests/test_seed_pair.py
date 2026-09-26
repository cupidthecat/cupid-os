import concurrent.futures
import copy
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import unittest
from tests.test_seed_release import DRAFT, ROOT, release, encode
from tests import test_seed_manifest as manifest_tests
from tests.test_seed_release_match import manifest


def original_pair():
    return ((ROOT / "bootstrap/seeds/i386-linux/manifest.json").read_bytes(),
            (ROOT / "bootstrap/seeds/i386-windows/manifest.json").read_bytes())


def bind(linux, windows=None):
    if isinstance(linux, dict): linux = encode(linux)
    windows = copy.deepcopy(windows) if windows is not None else manifest(2)
    windows["provenance"]["plan_seed_manifest_sha256"] = hashlib.sha256(linux).hexdigest()
    return linux, encode(windows)


class PairTests(unittest.TestCase):
    def test_utf8_promotion_pair_binds_conditional_assembly_parent(self):
        linux = manifest_tests.candidate_manifest(1)
        windows = manifest_tests.candidate_manifest(2)
        record = release()
        revision = "e4f2ed652e756b1abb375ec061923f5259799e01"
        linux_parent = "da26556401dd20d039ed1175f3bf857c4c8bebd50fb52b3bd75a06b95fdf41ed"
        windows_parent = "c715ce354c28b97c6b9c4e5702c98d368d07dff9e52bc2f0deb71ab3194d2395"
        for item in (record, linux["provenance"], windows["provenance"]):
            item["source_input_count"] = 73
        record.update(parent_source_revision=revision,
                      parent_linux_manifest_sha256=linux_parent,
                      parent_windows_manifest_sha256=windows_parent)
        linux["provenance"].update(parent_seed_source_revision=revision,
                                   parent_seed_manifest_sha256=linux_parent)
        windows["provenance"].update(
            parent_execution_seed_source_revision=revision,
            parent_execution_seed_manifest_sha256=windows_parent,
            parent_plan_seed_source_revision=revision,
            parent_plan_seed_manifest_sha256=linux_parent,
            native_build_plan_sha256=manifest_tests.seed._build_plan_sha256(
                manifest_tests.seed._windows_build_plan(linux["build_plan"], utf8=True)))
        record["windows_plan_sha256"] = windows["provenance"]["native_build_plan_sha256"]
        left, right = bind(linux, windows)
        self.check(record, left, right)
        self.check(release(), left, right, False)
        for key in ("parent_source_revision", "parent_linux_manifest_sha256",
                    "parent_windows_manifest_sha256"):
            wrong = copy.deepcopy(record)
            wrong[key] = release()[key]
            self.check(wrong, left, right, False)
        self.check(record, left + b" ", right, False)
        self.check(record, *bind(left + b" ", windows))

    def test_candidate_pair_binds_the_new_plan_and_source_count(self):
        linux = manifest_tests.candidate_manifest(1)
        windows = manifest_tests.candidate_manifest(2)
        record = release()
        record["source_input_count"] = 66
        record["linux_plan_sha256"] = linux["build_plan_sha256"]
        record["windows_plan_sha256"] = windows["provenance"]["native_build_plan_sha256"]
        for item in (record, linux["provenance"], windows["provenance"]):
            item["source_revision"] = "f" * 40
        linux_bytes, windows_bytes = bind(linux, windows)
        self.check(record, linux_bytes, windows_bytes)
        self.check(release(), linux_bytes, windows_bytes, False)
        for fmt in (1, 2):
            altered = copy.deepcopy(linux if fmt == 1 else windows)
            altered["provenance"]["source_input_count"] = 61
            if fmt == 1:
                left, right = bind(altered, windows)
            else:
                left, right = bind(linux, altered)
            self.check(record, left, right, False)

    records = []
    unload = classmethod(manifest_tests.ManifestTests.unload.__func__)

    @classmethod
    def setUpClass(cls):
        manifest_tests.ManifestTests.setUpClass.__func__(cls)
        cls.pair = cls.library.test_pair
        cls.pair.argtypes = [ctypes.c_void_p, ctypes.c_size_t] * 3 + [ctypes.c_void_p, ctypes.c_size_t]
        cls.pair.restype = ctypes.c_int

    def check(self, record, linux, windows, accepted=True, capacity=128):
        inputs = [encode(value) if isinstance(value, dict) else value for value in (record, linux, windows)]
        buffers = [ctypes.create_string_buffer(data) for data in inputs]
        before = [bytes(buffer) for buffer in buffers]
        error = ctypes.create_string_buffer(b"!" * (capacity + 8))
        arguments = []
        for buffer, data in zip(buffers, inputs): arguments.extend((buffer, len(data)))
        status = self.pair(*arguments, error, capacity)
        self.assertEqual(status, int(accepted), error.value)
        self.assertEqual([bytes(buffer) for buffer in buffers], before)
        self.assertEqual(bytes(error)[capacity:], b"!" * 8 + b"\0")
        if accepted and capacity: self.assertEqual(error.value, b"")
        if not accepted and capacity > 1: self.assertTrue(error.value)
        if os.environ.get("CUPID_PAIR_EXPORT"):
            self.records.append((*inputs, accepted))

    def test_current_pair_and_format_order(self):
        linux, windows = original_pair()
        self.check(release(), linux, windows)
        self.check(release(), windows, linux, False)
        self.check(release(), linux, linux, False)
        self.check(release(), windows, windows, False)

    def test_actual_bytes_drive_binding_across_every_sha_padding_residue(self):
        original_linux, original_windows = original_pair()
        compact = encode(manifest(1))
        self.check(release(), compact, original_windows, False)
        self.check(release(), *bind(compact))
        for count in range(1, 65):
            linux = original_linux + b" " * count
            self.check(release(), linux, original_windows, False)
            self.check(release(), *bind(linux))

    def test_escaped_and_reordered_pair_is_semantic_except_for_byte_binding(self):
        def escape(data):
            return b'"'.join(b"".join(f"\\u{byte:04x}".encode() for byte in part)
                             if index % 2 else part
                             for index, part in enumerate(data.split(b'"')))
        linux = manifest(1)
        linux["artifacts"].reverse()
        linux = escape(encode(dict(reversed(list(linux.items())))))
        linux, windows = bind(linux)
        self.check(release(), linux, escape(windows))

    def test_invalid_target_lineage_and_plan_cannot_hide_behind_release_match(self):
        for fmt in (1, 2):
            for location in (("target",), ("provenance", "producer_lineage")) + ((("build_plan",),) if fmt == 1 else ()):
                linux, windows = manifest(1), manifest(2)
                target = linux if fmt == 1 else windows
                for key in location[:-1]: target = target[key]
                target[location[-1]] = {}
                self.check(release(), *bind(linux, windows), False)
        linux = manifest(1)
        linux["build_plan"]["sources"][0]["gnu_extensions"] = False
        self.check(release(), *bind(linux), False)

    def test_every_changed_tool_manifest_is_rejected_after_rebinding(self):
        for fmt in (1, 2):
            directory = ROOT / "bootstrap/seeds" / ("i386-linux" if fmt == 1 else "i386-windows")
            for index in range(6):
                linux, windows = manifest(1), manifest(2)
                row = (linux if fmt == 1 else windows)["artifacts"][index]
                payload = bytearray((directory / row["file"]).read_bytes())
                payload[-1] ^= 1
                row["sha256"] = hashlib.sha256(payload).hexdigest()
                self.check(release(), *bind(linux, windows), False)

    def test_each_release_identity_and_mixed_generation(self):
        linux, windows = original_pair()
        for key, value in release().items():
            record = release()
            if key in ("schema", "artifacts"): continue
            record[key] = value + 1 if isinstance(value, int) else "e" * len(value)
            self.check(record, linux, windows, False)
        record = release()
        l, w = manifest_tests.historical_manifest(1), manifest_tests.historical_manifest(2)
        record["linux_plan_sha256"] = l["build_plan_sha256"]
        record["windows_plan_sha256"] = w["provenance"]["native_build_plan_sha256"]
        for item in (record, l["provenance"], w["provenance"]):
            item["source_revision"] = "a" * 40
            item["source_input_count"] = 61
        self.check(record, *bind(l, w))
        self.check(release(), *bind(l, w), False)
        self.check(record, *bind(l, manifest(2)), False)

    def test_bad_binding_release_and_trailing_input(self):
        linux, windows = original_pair()
        for index in range(3):
            values = [encode(release()), linux, windows]
            for invalid in (b"", b"bad", values[index][:-1], values[index] + b"x"):
                mutated = list(values)
                mutated[index] = invalid
                # Removing the original Windows/Linux trailing newline is valid
                # JSON; it still changes the binding only for the Linux input.
                if invalid == values[index][:-1] and values[index].endswith(b"\n"):
                    if index == 2:
                        self.check(*mutated)
                        continue
                self.check(*mutated, False)
        wrong = manifest(2)
        wrong["provenance"]["plan_seed_manifest_sha256"] = "0" * 64
        self.check(release(), linux, wrong, False)

    def test_null_arguments_and_bounded_diagnostics(self):
        linux, windows = original_pair()
        for capacity in (0, 1, 2, 8, 128):
            self.check(release(), linux, windows, True, capacity)
            self.check(release(), linux, b"bad", False, capacity)
        data = [encode(release()), linux, windows]
        buffers = [ctypes.create_string_buffer(value) for value in data]
        args = []
        for buffer, value in zip(buffers, data): args.extend((buffer, len(value)))
        for index in (0, 2, 4):
            bad = list(args)
            bad[index] = None
            self.assertEqual(self.pair(*bad, None, 0), 0)
        self.assertEqual(self.pair(*args, None, 1), 0)
        self.assertEqual(self.pair(*args, None, 0), 1)

    def test_concurrent_pairs_keep_their_own_release_and_digest(self):
        def run(index):
            record = release()
            linux, windows = manifest(1), manifest(2)
            for item in (record, linux["provenance"], windows["provenance"]):
                item["source_revision"] = f"{index:040x}"
            self.check(record, *bind(linux, windows))
            self.check(release(), *bind(linux, windows), False)
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
            list(pool.map(run, range(48)))


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(PairTests)
    outcome = unittest.TextTestRunner().run(suite)
    if outcome.wasSuccessful() and os.environ.get("CUPID_PAIR_EXPORT"):
        destination = Path(os.environ["CUPID_PAIR_EXPORT"])
        with destination.with_suffix(".bin").open("xb") as stream:
            for record, linux, windows, _ in PairTests.records:
                stream.write(struct.pack("<III", len(record), len(linux), len(windows)))
                stream.write(record + linux + windows)
        with destination.with_suffix(".txt").open("x") as stream:
            stream.write("".join("1\n" if row[3] else "0\n" for row in PairTests.records))
        print(f"Exported {len(PairTests.records)} combined seed-pair cases")
    sys.exit(not outcome.wasSuccessful())

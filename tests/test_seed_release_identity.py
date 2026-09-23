import copy
import hashlib
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock
from types import SimpleNamespace

from tests.test_seed_release import release
from tools import seed_release_identity as identity


ROOT = Path(__file__).resolve().parents[1]


class ReleaseIdentityTests(unittest.TestCase):
    def test_record_preserves_independent_existing_pins(self):
        self.assertEqual(identity.current_release_identity(), release())
        payload = identity.encode_release_identity()
        self.assertTrue(payload.endswith(b"\n"))
        self.assertEqual(identity.verify_release_identity_bytes(payload), release())

    def test_tracked_record_matches_pins(self):
        payload = (ROOT / identity.RELEASE_PATH).read_bytes()
        self.assertEqual(identity.verify_release_identity_bytes(payload), release())

    def test_order_and_escaped_keys_are_semantic(self):
        record = dict(reversed(list(release().items())))
        record["artifacts"].reverse()
        payload = json.dumps(record).replace('"schema"', '"sch\\u0065ma"').encode()
        self.assertEqual(identity.verify_release_identity_bytes(payload), release())

    def test_every_identity_is_independently_pinned(self):
        for name, value in release().items():
            if name == "artifacts":
                continue
            with self.subTest(field=name):
                record = release()
                record[name] = value + 1 if type(value) is int else "0" + value[1:]
                with self.assertRaises(identity.ReleaseIdentityError):
                    identity.verify_release_identity_bytes(json.dumps(record).encode())
        for index in range(12):
            for field in ("size", "sha256"):
                with self.subTest(index=index, field=field):
                    record = release()
                    row = record["artifacts"][index]
                    row[field] = row[field] + 1 if field == "size" else "f" * 64
                    with self.assertRaises(identity.ReleaseIdentityError):
                        identity.verify_release_identity_bytes(json.dumps(record).encode())

    def test_missing_extra_and_duplicate_rows_and_keys(self):
        valid = json.dumps(release())
        cases = [valid.replace('"schema":', '"schema":"duplicate","schema":', 1),
                 valid.replace('"schema":', '"sch\\u0065ma":"duplicate","schema":', 1)]
        for change in ("missing", "extra", "duplicate"):
            record = release()
            if change == "missing":
                del record["source_revision"]
            elif change == "extra":
                record["extra"] = 1
            else:
                record["artifacts"][1] = copy.deepcopy(record["artifacts"][0])
            cases.append(json.dumps(record))
        record = release()
        record["artifacts"][0]["extra"] = 1
        cases.append(json.dumps(record))
        for payload in cases:
            with self.subTest(payload=payload[:80]):
                with self.assertRaises(identity.ReleaseIdentityError):
                    identity.verify_release_identity_bytes(payload.encode())

    def test_non_integer_numbers_invalid_encoding_and_bounds(self):
        cases = [b"", b"[]", b"null", b"\xff", b" " * 65537,
                 b'{"a":NaN}', b'{"a":Infinity}', b'{"a":1.0}',
                 b"[" * 2000 + b"]" * 2000]
        for field in ("source_input_count", "size"):
            for value in (True, 59.0, None, "59"):
                record = release()
                target = record if field == "source_input_count" else record["artifacts"][0]
                target[field] = value
                cases.append(json.dumps(record).encode())
        for payload in cases:
            with self.subTest(payload=payload[:40]):
                with self.assertRaises(identity.ReleaseIdentityError):
                    identity.verify_release_identity_bytes(payload)

    def test_returned_records_do_not_share_mutable_state(self):
        first = identity.current_release_identity()
        first["artifacts"][0]["size"] = 1
        self.assertEqual(identity.current_release_identity(), release())

    def test_author_checks_both_cohorts_and_binding_before_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "release.json"
            with mock.patch.object(identity, "checked_release_bytes", side_effect=identity.ReleaseIdentityError("bad pair")):
                with self.assertRaisesRegex(identity.ReleaseIdentityError, "bad pair"):
                    identity.write_release_candidate(ROOT, output)
            self.assertFalse(output.exists())

    def test_author_preserves_existing_candidate(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "release.json"
            output.write_bytes(b"previous")
            with mock.patch.object(identity, "checked_release_bytes", return_value=identity.encode_release_identity()):
                with self.assertRaises(FileExistsError):
                    identity.write_release_candidate(ROOT, output)
            self.assertEqual(output.read_bytes(), b"previous")

    def test_checked_author_accepts_installed_pair(self):
        self.assertEqual(identity.checked_release_bytes(ROOT), identity.encode_release_identity())

    def test_author_rejects_historical_seed_schemas_before_pair_binding(self):
        for host in ("linux", "windows"):
            linux = SimpleNamespace(manifest={"schema": identity.seed.PROMOTED_SEED_SCHEMA})
            windows = SimpleNamespace(manifest={"schema": identity.seed.PROMOTED_WINDOWS_SEED_SCHEMA})
            selected = linux if host == "linux" else windows
            selected.manifest["schema"] = (
                identity.seed.SEED_SCHEMA if host == "linux" else identity.seed.WINDOWS_SEED_SCHEMA
            )
            with self.subTest(host=host), mock.patch.object(
                identity.seed, "verify_seed_inputs", side_effect=(linux, windows)
            ), mock.patch.object(identity.seed, "require_live_seed_inputs") as recheck:
                with self.assertRaisesRegex(identity.ReleaseIdentityError, "promoted six-tool"):
                    identity.checked_release_bytes(ROOT)
                recheck.assert_not_called()

    def test_reformatted_pair_requires_binding_to_actual_linux_bytes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for host in ("linux", "windows"):
                name = f"bootstrap/seeds/i386-{host}"
                shutil.copytree(ROOT / name, root / name)
            linux = root / "bootstrap/seeds/i386-linux/manifest.json"
            value = json.loads(linux.read_bytes())
            value["artifacts"].reverse()
            linux.write_bytes(json.dumps(value, separators=(",", ":")).encode())
            with self.assertRaisesRegex(identity.ReleaseIdentityError, "not bound"):
                identity.checked_release_bytes(root)
            windows = root / "bootstrap/seeds/i386-windows/manifest.json"
            value = json.loads(windows.read_bytes())
            value["provenance"]["plan_seed_manifest_sha256"] = hashlib.sha256(linux.read_bytes()).hexdigest()
            value["artifacts"].reverse()
            windows.write_bytes(json.dumps(value, separators=(",", ":")).encode())
            self.assertEqual(identity.checked_release_bytes(root), identity.encode_release_identity())

    def test_matching_changed_manifest_digest_cannot_author_changed_tool(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for host in ("linux", "windows"):
                name = f"bootstrap/seeds/i386-{host}"
                shutil.copytree(ROOT / name, root / name)
            tool = root / "bootstrap/seeds/i386-windows/cupidbuild.exe"
            data = bytearray(tool.read_bytes())
            data[-1] ^= 1
            tool.write_bytes(data)
            manifest = tool.parent / "manifest.json"
            value = json.loads(manifest.read_bytes())
            for row in value["artifacts"]:
                if row["name"] == "cupidbuild":
                    row["sha256"] = hashlib.sha256(data).hexdigest()
            manifest.write_bytes(json.dumps(value).encode())
            output = root / "candidate.json"
            with self.assertRaises(identity.ReleaseIdentityError):
                identity.write_release_candidate(root, output)
            self.assertFalse(output.exists())


if __name__ == "__main__":
    unittest.main()

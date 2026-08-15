import json
import os
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SOURCE = REPO_ROOT / "toolchain/tests/artifact_size_policy_contract.cc"
MAGIC = b"CUPSIZE1"
MANIFEST_PATH = "bootstrap/seeds/i386-linux/manifest.json"
SEED_TOOLS = (
    ("cupidasm", "CupidASM", 101),
    ("cupidc", "CupidC", 102),
    ("cupiddis", "CupidDis", 103),
    ("cupidld", "CupidLD", 104),
    ("cupidobj", "CupidObj", 105),
)
FIXED_ARTIFACTS = (
    ("boot/boot.bin", "CupidASM", 10),
    ("kernel/kernel.bin", "CupidObj", 20),
    ("kernel/kernel.elf", "CupidLD", 30),
    ("kernel/kernel.elf.pass1", "CupidLD", 40),
)
SUCCESS_REPORT = (
    '{"artifact_count":9,'
    '"schema":"cupid.artifact-size-verification.v1",'
    '"total_exact_bytes":615}\n'
)


def _host_compiler():
    configured = os.environ.get("CC")
    candidates = [configured] if configured else []
    candidates += ["clang", "gcc", "cc"]
    for candidate in candidates:
        if candidate and shutil.which(candidate):
            return candidate
    raise unittest.SkipTest("a hosted C compiler is required")


def _build_contract(build):
    suffix = ".exe" if os.name == "nt" else ""
    output = build / ("artifact-size-policy-contract" + suffix)
    command = [
        _host_compiler(),
        "-std=c11",
        "-O2",
        "-pedantic",
        "-Werror",
        "-Wall",
        "-Wextra",
        "-Wshadow",
        "-Wpointer-arith",
        "-Wcast-qual",
        "-Wstrict-prototypes",
        "-Wmissing-prototypes",
        "-Wconversion",
        "-Wsign-conversion",
        "-D_CRT_SECURE_NO_WARNINGS",
        "-x",
        "c",
        str(SOURCE),
        "-o",
        str(output),
    ]
    result = subprocess.run(
        command,
        cwd=REPO_ROOT,
        text=True,
        capture_output=True,
        timeout=60,
    )
    if result.returncode != 0:
        raise AssertionError(
            "artifact-size contract hosted build failed\n"
            + result.stdout
            + result.stderr
        )
    return output


def _json_bytes(value):
    return json.dumps(
        value,
        ensure_ascii=False,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("utf-8")


def _manifest():
    return {
        "artifacts": [
            {
                "file": f"{name}.elf",
                "name": name,
                "producer": name in {"cupidasm", "cupidc", "cupidld"},
                "sha256": name * 8,
                "size": size,
            }
            for name, _owner, size in SEED_TOOLS
        ],
        "build_plan": {
            "ignored": [None, True, False, -3, 1.25, {"nested": "value"}]
        },
        "schema": "cupid.bootstrap-seed.v1",
        "target": {"architecture": "i386", "format": "elf32"},
    }


def _policy(manifest_path=MANIFEST_PATH):
    parent = manifest_path.rsplit("/", 1)[0] if "/" in manifest_path else ""
    artifacts = [
        {
            "exact_bytes": size,
            "path": path,
            "producer": owner,
            "reason": "Reviewed exact output.",
        }
        for path, owner, size in FIXED_ARTIFACTS
    ]
    artifacts += [
        {
            "exact_bytes": size,
            "path": f"{parent + '/' if parent else ''}{name}.elf",
            "producer": owner,
            "reason": "Reviewed seed output.",
        }
        for name, owner, size in SEED_TOOLS
    ]
    artifacts.sort(key=lambda entry: entry["path"])
    return {
        "artifacts": artifacts,
        "schema": "cupid.artifact-size-policy.v1",
    }


def _observations(policy):
    return [
        (entry["path"], 1, entry["exact_bytes"])
        for entry in policy["artifacts"]
    ]


def _append_bytes(payload, value):
    payload.extend(struct.pack("<I", len(value)))
    payload.extend(value)


def _request(
    *,
    policy=None,
    policy_bytes=None,
    manifest_path=MANIFEST_PATH,
    manifest=None,
    manifest_bytes=None,
    observations=None,
    trailing=b"",
):
    if policy is None:
        policy = _policy(manifest_path)
    if manifest is None:
        manifest = _manifest()
    if observations is None:
        observations = _observations(policy)
    if policy_bytes is None:
        policy_bytes = _json_bytes(policy)
    if manifest_bytes is None:
        manifest_bytes = _json_bytes(manifest)
    payload = bytearray(MAGIC)
    _append_bytes(payload, policy_bytes)
    _append_bytes(payload, manifest_path.encode("utf-8"))
    _append_bytes(payload, manifest_bytes)
    payload.extend(struct.pack("<I", len(observations)))
    for path, kind, size in observations:
        _append_bytes(payload, path.encode("utf-8"))
        payload.extend(struct.pack("<I", kind))
        payload.extend(struct.pack("<Q", size))
    payload.extend(trailing)
    return bytes(payload)


class ArtifactSizePolicyContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.contract_build = tempfile.TemporaryDirectory(
            prefix=".artifact-size-contract-", dir=REPO_ROOT
        )
        cls.contract = _build_contract(Path(cls.contract_build.name))

    @classmethod
    def tearDownClass(cls):
        cls.contract_build.cleanup()

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.request_path = Path(self.temporary.name) / "request.bin"

    def tearDown(self):
        self.temporary.cleanup()

    def run_request(self, payload):
        self.request_path.write_bytes(payload)
        return subprocess.run(
            [self.contract, "check", self.request_path],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=10,
        )

    def assert_contract_failure(self, payload):
        result = self.run_request(payload)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertTrue(
            result.stderr.startswith("Cupid artifact-size contract failed:"),
            result.stderr,
        )

    def test_valid_snapshot_emits_canonical_report(self):
        result = self.run_request(_request())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, SUCCESS_REPORT)
        self.assertEqual(result.stderr, "")

    def test_report_is_deterministic(self):
        first = self.run_request(_request())
        second = self.run_request(_request())
        self.assertEqual(first.returncode, 0, first.stderr)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(first.stdout, SUCCESS_REPORT)
        self.assertEqual(second.stdout, first.stdout)

    def test_observed_size_must_match_policy(self):
        policy = _policy()
        observations = _observations(policy)
        path, kind, size = observations[0]
        observations[0] = (path, kind, size + 1)
        self.assert_contract_failure(
            _request(policy=policy, observations=observations)
        )

    def test_policy_and_manifest_schemas_are_checked(self):
        policy = _policy()
        policy["schema"] = "cupid.artifact-size-policy.v0"
        self.assert_contract_failure(_request(policy=policy))

        manifest = _manifest()
        manifest["schema"] = "cupid.bootstrap-seed.v0"
        self.assert_contract_failure(_request(manifest=manifest))

    def test_policy_requires_exact_object_keys(self):
        policy = _policy()
        policy["unexpected"] = True
        self.assert_contract_failure(_request(policy=policy))

        policy = _policy()
        del policy["schema"]
        self.assert_contract_failure(_request(policy=policy))

        policy = _policy()
        policy["artifacts"][0]["unexpected"] = True
        self.assert_contract_failure(_request(policy=policy))

        policy = _policy()
        del policy["artifacts"][0]["reason"]
        self.assert_contract_failure(_request(policy=policy))

    def test_fixed_and_seed_owners_are_checked(self):
        policy = _policy()
        policy["artifacts"][0]["producer"] = "CupidLD"
        self.assert_contract_failure(_request(policy=policy))

        policy = _policy()
        seed_entry = next(
            entry
            for entry in policy["artifacts"]
            if entry["path"].endswith("/cupidc.elf")
        )
        seed_entry["producer"] = "CupidASM"
        self.assert_contract_failure(_request(policy=policy))

    def test_policy_seed_sizes_must_match_selected_manifest(self):
        policy = _policy()
        seed_entry = next(
            entry
            for entry in policy["artifacts"]
            if entry["path"].endswith("/cupidobj.elf")
        )
        seed_entry["exact_bytes"] += 1
        observations = _observations(policy)
        self.assert_contract_failure(
            _request(policy=policy, observations=observations)
        )

    def test_seed_manifest_requires_five_named_elf_artifacts(self):
        manifest = _manifest()
        manifest["artifacts"][0]["file"] = "wrong.elf"
        self.assert_contract_failure(_request(manifest=manifest))

        manifest = _manifest()
        manifest["artifacts"].pop()
        self.assert_contract_failure(_request(manifest=manifest))

        manifest = _manifest()
        manifest["artifacts"][-1]["name"] = "cupidasm"
        manifest["artifacts"][-1]["file"] = "cupidasm.elf"
        self.assert_contract_failure(_request(manifest=manifest))

        manifest = _manifest()
        manifest["artifacts"][-1]["name"] = "other"
        manifest["artifacts"][-1]["file"] = "other.elf"
        self.assert_contract_failure(_request(manifest=manifest))

    def test_observation_cohort_rejects_duplicate_missing_and_unknown_paths(self):
        policy = _policy()
        observations = _observations(policy)

        duplicate = observations[:-1] + [observations[0]]
        self.assert_contract_failure(
            _request(policy=policy, observations=duplicate)
        )

        self.assert_contract_failure(
            _request(policy=policy, observations=observations[:-1])
        )

        unknown = list(observations)
        _path, kind, size = unknown[-1]
        unknown[-1] = ("unknown/output.bin", kind, size)
        self.assert_contract_failure(
            _request(policy=policy, observations=unknown)
        )

    def test_policy_rejects_duplicate_missing_unknown_and_unsorted_paths(self):
        policy = _policy()
        policy["artifacts"][-1]["path"] = policy["artifacts"][0]["path"]
        self.assert_contract_failure(_request(policy=policy))

        policy = _policy()
        policy["artifacts"].pop()
        self.assert_contract_failure(_request(policy=policy))

        policy = _policy()
        policy["artifacts"][-1]["path"] = "unknown/output.bin"
        self.assert_contract_failure(_request(policy=policy))

        policy = _policy()
        policy["artifacts"][0], policy["artifacts"][1] = (
            policy["artifacts"][1],
            policy["artifacts"][0],
        )
        self.assert_contract_failure(_request(policy=policy))

    def test_malformed_json_and_integer_values_are_rejected(self):
        self.assert_contract_failure(_request(policy_bytes=b'{"schema":'))
        self.assert_contract_failure(_request(manifest_bytes=b"[] trailing"))

        for value in (0, -1, 1.5, True, 1 << 80):
            with self.subTest(value=value):
                policy = _policy()
                policy["artifacts"][0]["exact_bytes"] = value
                self.assert_contract_failure(
                    _request(
                        policy=policy,
                        observations=_observations(_policy()),
                    )
                )

        manifest = _manifest()
        manifest["artifacts"][0]["size"] = 1.5
        self.assert_contract_failure(_request(manifest=manifest))

    def test_paths_must_be_canonical_and_safe(self):
        for unsafe in (
            "/boot/boot.bin",
            "kernel/../kernel.bin",
            "kernel//kernel.bin",
            "kernel\\kernel.bin",
            "./boot/boot.bin",
        ):
            with self.subTest(path=unsafe):
                policy = _policy()
                policy["artifacts"][0]["path"] = unsafe
                self.assert_contract_failure(_request(policy=policy))

        self.assert_contract_failure(_request(manifest_path="../manifest.json"))

    def test_policy_requires_nonempty_producer_and_reason(self):
        for field, value in (
            ("producer", ""),
            ("reason", ""),
            ("reason", " leading"),
            ("reason", "trailing "),
            ("reason", "two\nlines"),
        ):
            with self.subTest(field=field, value=value):
                policy = _policy()
                policy["artifacts"][0][field] = value
                self.assert_contract_failure(_request(policy=policy))

    def test_every_observation_must_be_regular(self):
        policy = _policy()
        for kind in (2, 3, 4, 0, 5):
            with self.subTest(kind=kind):
                observations = _observations(policy)
                path, _regular, size = observations[0]
                observations[0] = (path, kind, size)
                self.assert_contract_failure(
                    _request(policy=policy, observations=observations)
                )

    def test_truncated_and_trailing_snapshots_are_rejected(self):
        request = _request()
        for cut in (0, 1, 7, 8, 12, len(request) - 1):
            with self.subTest(cut=cut):
                self.assert_contract_failure(request[:cut])
        self.assert_contract_failure(_request(trailing=b"unexpected"))

    def test_usage_is_a_controlled_failure(self):
        for arguments in ((), ("check",), ("other", str(self.request_path))):
            with self.subTest(arguments=arguments):
                result = subprocess.run(
                    [self.contract, *arguments],
                    cwd=REPO_ROOT,
                    text=True,
                    capture_output=True,
                    timeout=10,
                )
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, "")
                self.assertTrue(
                    result.stderr.startswith(
                        "Cupid artifact-size contract failed:"
                    ),
                    result.stderr,
                )


if __name__ == "__main__":
    unittest.main()

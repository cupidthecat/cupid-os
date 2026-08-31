import hashlib
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
MAGIC = b"CUPSIZE2"
MANIFEST_PATH = "bootstrap/seeds/i386-linux/manifest.json"
WINDOWS_MANIFEST_PATH = "bootstrap/seeds/i386-windows/manifest.json"
SOURCE_REVISION = "1" * 40
SOURCE_SNAPSHOT = "2" * 64
SEED_TOOLS = (
    ("cupidasm", "CupidASM", 101),
    ("cupidbuild", "CupidBuild", 102),
    ("cupidc", "CupidC", 103),
    ("cupiddis", "CupidDis", 104),
    ("cupidld", "CupidLD", 105),
    ("cupidobj", "CupidObj", 106),
)
FIXED_ARTIFACTS = (
    ("boot/boot.bin", "CupidASM", 10),
    ("bootstrap/seeds/i386-windows/cupidasm.exe", "CupidASM", 51),
    ("bootstrap/seeds/i386-windows/cupidbuild.exe", "CupidBuild", 52),
    ("bootstrap/seeds/i386-windows/cupidc.exe", "CupidC", 53),
    ("bootstrap/seeds/i386-windows/cupiddis.exe", "CupidDis", 54),
    ("bootstrap/seeds/i386-windows/cupidld.exe", "CupidLD", 55),
    ("bootstrap/seeds/i386-windows/cupidobj.exe", "CupidObj", 56),
    ("kernel/kernel.bin", "CupidObj", 20),
    ("kernel/kernel.elf", "CupidLD", 30),
    ("kernel/kernel.elf.pass1", "CupidLD", 40),
)
SUCCESS_REPORT = (
    '{"artifact_count":16,'
    '"schema":"cupid.artifact-size-verification.v1",'
    '"total_exact_bytes":1042}\n'
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
                "sha256": hashlib.sha256(name.encode("ascii")).hexdigest(),
                "size": size,
            }
            for name, _owner, size in SEED_TOOLS
        ],
        "build_plan": {
            "ignored": [None, True, False, -3, 1.25, {"nested": "value"}]
        },
        "provenance": {
            "artifact_generation": "paired-stage-four-six-tool",
            "fixed_point_command": "make bootstrap-from-seed",
            "fixed_point_result": "pass",
            "parent_seed_manifest_sha256": (
                "770f979407f930deba0c9ba887bcd14f2350a785b1c0df6b31ddc2659c46eaae"
            ),
            "parent_seed_source_revision": (
                "9d10c223fc7aa22901e6f4ae81ce800ff1b62ad6"
            ),
            "producer_lineage": {
                "assembly": "stage-three CupidASM from the checked-seed bootstrap",
                "c": "stage-three CupidC from the checked-seed bootstrap",
                "link": "stage-three CupidLD from the checked-seed bootstrap",
            },
            "seed_generation": "stage-four",
            "source_input_count": 58,
            "source_revision": SOURCE_REVISION,
            "source_snapshot_sha256": SOURCE_SNAPSHOT,
        },
        "schema": "cupid.bootstrap-seed.v2",
        "target": {"architecture": "i386", "format": "elf32"},
    }


def _windows_manifest(parent_manifest_sha256):
    return {
        "artifacts": [
            {
                "file": f"{name}.exe",
                "name": name,
                "producer": name in {"cupidasm", "cupidc", "cupidld"},
                "sha256": hashlib.sha256(name.encode("ascii")).hexdigest(),
                "size": size,
            }
            for name, _owner, size in SEED_TOOLS
            for fixed_path, _fixed_owner, fixed_size in FIXED_ARTIFACTS
            if fixed_path.endswith(f"/{name}.exe")
            for size in (fixed_size,)
        ],
        "provenance": {
            "artifact_generation": "paired-stage-four-six-tool-native-windows",
            "fixed_point_command": "make bootstrap-windows-from-seed",
            "fixed_point_result": "pass",
            "linux_candidate_build_plan_sha256": (
                "52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817bebc35c9904efbecbd"
            ),
            "native_build_plan_sha256": (
                "f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c995086bfe723cff14"
            ),
            "parent_execution_seed_manifest_sha256": (
                "bf6147cf2e8249372869a24e5b8477ffb785d9a48eef80209366cfbaff19c7db"
            ),
            "parent_execution_seed_source_revision": (
                "9d10c223fc7aa22901e6f4ae81ce800ff1b62ad6"
            ),
            "parent_plan_seed_manifest_sha256": (
                "770f979407f930deba0c9ba887bcd14f2350a785b1c0df6b31ddc2659c46eaae"
            ),
            "parent_plan_seed_source_revision": (
                "9d10c223fc7aa22901e6f4ae81ce800ff1b62ad6"
            ),
            "plan_seed_manifest_sha256": parent_manifest_sha256,
            "producer_lineage": {
                "assembly": (
                    "native stage-three CupidASM from the checked i386 "
                    "Windows bootstrap"
                ),
                "c": (
                    "native stage-three CupidC from the checked i386 "
                    "Windows bootstrap"
                ),
                "link": (
                    "native stage-three CupidLD from the checked i386 "
                    "Windows bootstrap"
                ),
            },
            "source_input_count": 58,
            "source_revision": SOURCE_REVISION,
            "source_snapshot_sha256": SOURCE_SNAPSHOT,
        },
        "schema": "cupid.execution-seed.v2",
        "target": {
            "abi": "windows-stdcall-imports",
            "architecture": "i386",
            "byte_order": "little",
            "entry": 4198400,
            "linkage": "kernel32-imports",
            "operating_system": "windows",
            "pe_class": 32,
        },
    }


def _windows_observations(windows_manifest):
    return [
        (
            f"bootstrap/seeds/i386-windows/{artifact['file']}",
            1,
            artifact["size"],
            artifact["sha256"],
        )
        for artifact in windows_manifest["artifacts"]
    ]


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
    linux_manifest_digest=None,
    windows_manifest_path=WINDOWS_MANIFEST_PATH,
    windows_manifest=None,
    windows_manifest_bytes=None,
    windows_observations=None,
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
    if linux_manifest_digest is None:
        linux_manifest_digest = hashlib.sha256(manifest_bytes).hexdigest()
    if windows_manifest is None:
        windows_manifest = _windows_manifest(linux_manifest_digest)
    if windows_observations is None:
        windows_observations = _windows_observations(windows_manifest)
    if windows_manifest_bytes is None:
        windows_manifest_bytes = _json_bytes(windows_manifest)
    payload = bytearray(MAGIC)
    _append_bytes(payload, policy_bytes)
    _append_bytes(payload, manifest_path.encode("utf-8"))
    _append_bytes(payload, manifest_bytes)
    _append_bytes(payload, linux_manifest_digest.encode("ascii"))
    _append_bytes(payload, windows_manifest_path.encode("utf-8"))
    _append_bytes(payload, windows_manifest_bytes)
    payload.extend(struct.pack("<I", len(windows_observations)))
    for path, kind, size, digest in windows_observations:
        _append_bytes(payload, path.encode("utf-8"))
        payload.extend(struct.pack("<I", kind))
        payload.extend(struct.pack("<Q", size))
        _append_bytes(payload, digest.encode("ascii"))
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

    def test_promoted_59_input_seed_pair_is_accepted(self):
        manifest = _manifest()
        manifest["provenance"]["source_input_count"] = 59
        manifest["provenance"]["parent_seed_manifest_sha256"] = (
            "770f979407f930deba0c9ba887bcd14f2350a785b1c0df6b31ddc2659c46eaae"
        )
        manifest["provenance"]["parent_seed_source_revision"] = (
            "9d10c223fc7aa22901e6f4ae81ce800ff1b62ad6"
        )
        manifest_bytes = _json_bytes(manifest)
        manifest_digest = hashlib.sha256(manifest_bytes).hexdigest()
        windows_manifest = _windows_manifest(manifest_digest)
        windows_manifest["provenance"]["source_input_count"] = 59
        windows_manifest["provenance"][
            "parent_execution_seed_manifest_sha256"
        ] = (
            "bf6147cf2e8249372869a24e5b8477ffb785d9a48eef80209366cfbaff19c7db"
        )
        windows_manifest["provenance"][
            "parent_execution_seed_source_revision"
        ] = "9d10c223fc7aa22901e6f4ae81ce800ff1b62ad6"
        windows_manifest["provenance"][
            "parent_plan_seed_manifest_sha256"
        ] = (
            "770f979407f930deba0c9ba887bcd14f2350a785b1c0df6b31ddc2659c46eaae"
        )
        windows_manifest["provenance"][
            "parent_plan_seed_source_revision"
        ] = "9d10c223fc7aa22901e6f4ae81ce800ff1b62ad6"

        result = self.run_request(
            _request(
                manifest=manifest,
                manifest_bytes=manifest_bytes,
                linux_manifest_digest=manifest_digest,
                windows_manifest=windows_manifest,
            )
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, SUCCESS_REPORT)
        self.assertEqual(result.stderr, "")

    def test_active_seed_parent_pair_is_accepted_for_the_next_promotion(self):
        revision = "0232cb57aad5d6bdfd7bd77499762514b2f0ebfd"
        manifest = _manifest()
        manifest["provenance"].update(
            {
                "parent_seed_manifest_sha256": (
                    "470fcd1b8b1a1506f26d3dd33d51f55d6896571aacb7329b792d4612f9434781"
                ),
                "parent_seed_source_revision": revision,
            }
        )
        manifest_bytes = _json_bytes(manifest)
        manifest_digest = hashlib.sha256(manifest_bytes).hexdigest()
        windows_manifest = _windows_manifest(manifest_digest)
        windows_manifest["provenance"].update(
            {
                "parent_execution_seed_manifest_sha256": (
                    "e7e65908eb03eec43e44e2946b395723b164f5701d980aae8ffaaf1006c3d7e4"
                ),
                "parent_execution_seed_source_revision": revision,
                "parent_plan_seed_manifest_sha256": (
                    "470fcd1b8b1a1506f26d3dd33d51f55d6896571aacb7329b792d4612f9434781"
                ),
                "parent_plan_seed_source_revision": revision,
            }
        )

        result = self.run_request(
            _request(
                manifest=manifest,
                manifest_bytes=manifest_bytes,
                linux_manifest_digest=manifest_digest,
                windows_manifest=windows_manifest,
            )
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, SUCCESS_REPORT)
        self.assertEqual(result.stderr, "")

    def test_promoted_linux_parent_digest_and_revision_cannot_be_mixed(self):
        manifest = _manifest()
        manifest["provenance"]["parent_seed_manifest_sha256"] = (
            "470fcd1b8b1a1506f26d3dd33d51f55d6896571aacb7329b792d4612f9434781"
        )
        self.assert_contract_failure(_request(manifest=manifest))

    def test_promoted_windows_parent_digest_and_revision_cannot_be_mixed(self):
        promoted_parents = (
            (
                "parent_execution_seed_manifest_sha256",
                "e7e65908eb03eec43e44e2946b395723b164f5701d980aae8ffaaf1006c3d7e4",
            ),
            (
                "parent_plan_seed_manifest_sha256",
                "470fcd1b8b1a1506f26d3dd33d51f55d6896571aacb7329b792d4612f9434781",
            ),
        )
        for field, digest in promoted_parents:
            with self.subTest(field=field):
                manifest = _manifest()
                manifest_bytes = _json_bytes(manifest)
                manifest_digest = hashlib.sha256(manifest_bytes).hexdigest()
                windows_manifest = _windows_manifest(manifest_digest)
                windows_manifest["provenance"][field] = digest
                self.assert_contract_failure(
                    _request(
                        manifest=manifest,
                        manifest_bytes=manifest_bytes,
                        linux_manifest_digest=manifest_digest,
                        windows_manifest=windows_manifest,
                    )
                )

    def test_windows_execution_and_plan_parents_must_share_a_generation(self):
        active_revision = "0232cb57aad5d6bdfd7bd77499762514b2f0ebfd"
        active_parents = (
            {
                "parent_execution_seed_manifest_sha256": (
                    "e7e65908eb03eec43e44e2946b395723b164f5701d980aae8ffaaf1006c3d7e4"
                ),
                "parent_execution_seed_source_revision": active_revision,
            },
            {
                "parent_plan_seed_manifest_sha256": (
                    "470fcd1b8b1a1506f26d3dd33d51f55d6896571aacb7329b792d4612f9434781"
                ),
                "parent_plan_seed_source_revision": active_revision,
            },
        )
        for update in active_parents:
            with self.subTest(field=next(iter(update))):
                manifest = _manifest()
                manifest_bytes = _json_bytes(manifest)
                manifest_digest = hashlib.sha256(manifest_bytes).hexdigest()
                windows_manifest = _windows_manifest(manifest_digest)
                windows_manifest["provenance"].update(update)
                self.assert_contract_failure(
                    _request(
                        manifest=manifest,
                        manifest_bytes=manifest_bytes,
                        linux_manifest_digest=manifest_digest,
                        windows_manifest=windows_manifest,
                    )
                )

    def test_retired_v1_linux_parent_pair_is_rejected_for_v2(self):
        manifest = _manifest()
        manifest["provenance"].update(
            {
                "parent_seed_manifest_sha256": (
                    "b6e34a2e18dd18aba91c6358116eafde39953566efeadb224575ac8c13ab2c1b"
                ),
                "parent_seed_source_revision": (
                    "a17c9465911da41d59b7ada71733d36c39faa5ea"
                ),
            }
        )

        self.assert_contract_failure(_request(manifest=manifest))

    def test_retired_v1_windows_parent_pairs_are_rejected_for_v2(self):
        retired_parents = (
            (
                "parent_execution_seed_manifest_sha256",
                "parent_execution_seed_source_revision",
                "751e1d7787a4be08e4e86814bbb7473979fe2eb8a3292baed0241967f772eaef",
            ),
            (
                "parent_plan_seed_manifest_sha256",
                "parent_plan_seed_source_revision",
                "b6e34a2e18dd18aba91c6358116eafde39953566efeadb224575ac8c13ab2c1b",
            ),
        )
        for digest_field, revision_field, digest in retired_parents:
            with self.subTest(field=digest_field):
                manifest = _manifest()
                manifest_bytes = _json_bytes(manifest)
                manifest_digest = hashlib.sha256(manifest_bytes).hexdigest()
                windows_manifest = _windows_manifest(manifest_digest)
                windows_manifest["provenance"].update(
                    {
                        digest_field: digest,
                        revision_field: (
                            "a17c9465911da41d59b7ada71733d36c39faa5ea"
                        ),
                    }
                )
                self.assert_contract_failure(
                    _request(
                        manifest=manifest,
                        manifest_bytes=manifest_bytes,
                        linux_manifest_digest=manifest_digest,
                        windows_manifest=windows_manifest,
                    )
                )

    def test_seed_pair_requires_matching_source_input_counts(self):
        for linux_count, windows_count in ((58, 59), (59, 58)):
            with self.subTest(
                linux_count=linux_count, windows_count=windows_count
            ):
                manifest = _manifest()
                manifest["provenance"]["source_input_count"] = linux_count
                manifest_bytes = _json_bytes(manifest)
                manifest_digest = hashlib.sha256(manifest_bytes).hexdigest()
                windows_manifest = _windows_manifest(manifest_digest)
                windows_manifest["provenance"][
                    "source_input_count"
                ] = windows_count
                self.assert_contract_failure(
                    _request(
                        manifest=manifest,
                        manifest_bytes=manifest_bytes,
                        linux_manifest_digest=manifest_digest,
                        windows_manifest=windows_manifest,
                    )
                )

    def test_observed_size_must_match_policy(self):
        policy = _policy()
        observations = _observations(policy)
        path, kind, size = observations[0]
        observations[0] = (path, kind, size + 1)
        self.assert_contract_failure(
            _request(policy=policy, observations=observations)
        )

    def test_observation_count_diagnostic_names_full_cohort(self):
        policy = _policy()
        observations = _observations(policy)[:-1]
        result = self.run_request(
            _request(policy=policy, observations=observations)
        )
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertEqual(
            result.stderr,
            "Cupid artifact-size contract failed: request does not contain "
            "sixteen artifact observations\n",
        )

    def test_policy_and_manifest_schemas_are_checked(self):
        policy = _policy()
        policy["schema"] = "cupid.artifact-size-policy.v0"
        self.assert_contract_failure(_request(policy=policy))

        manifest = _manifest()
        manifest["schema"] = "cupid.bootstrap-seed.v0"
        self.assert_contract_failure(_request(manifest=manifest))

        windows_manifest = _windows_manifest("3" * 64)
        windows_manifest["schema"] = "cupid.execution-seed.v0"
        self.assert_contract_failure(
            _request(
                linux_manifest_digest="3" * 64,
                windows_manifest=windows_manifest,
            )
        )

    def test_linux_seed_v2_provenance_is_checked(self):
        cases = (
            ("artifact_generation", "wrong"),
            ("parent_seed_manifest_sha256", "0" * 64),
            ("parent_seed_source_revision", "0" * 40),
            ("source_input_count", 57),
            ("source_input_count", 60),
        )
        for field, value in cases:
            with self.subTest(field=field):
                manifest = _manifest()
                manifest["provenance"][field] = value
                self.assert_contract_failure(_request(manifest=manifest))

        manifest = _manifest()
        manifest["provenance"]["producer_lineage"]["c"] = "wrong"
        self.assert_contract_failure(_request(manifest=manifest))

    def test_windows_seed_target_parent_and_source_revision_are_checked(self):
        digest = "3" * 64
        windows_manifest = _windows_manifest(digest)
        windows_manifest["target"]["abi"] = "wrong"
        self.assert_contract_failure(
            _request(
                linux_manifest_digest=digest,
                windows_manifest=windows_manifest,
            )
        )

    def test_windows_seed_v2_plan_and_parent_provenance_are_checked(self):
        digest = "3" * 64
        cases = (
            ("linux_candidate_build_plan_sha256", "0" * 64),
            ("native_build_plan_sha256", "0" * 64),
            ("parent_execution_seed_manifest_sha256", "0" * 64),
            ("parent_execution_seed_source_revision", "0" * 40),
            ("parent_plan_seed_manifest_sha256", "0" * 64),
            ("parent_plan_seed_source_revision", "0" * 40),
            ("source_input_count", 57),
            ("source_input_count", 60),
        )
        for field, value in cases:
            with self.subTest(field=field):
                windows_manifest = _windows_manifest(digest)
                windows_manifest["provenance"][field] = value
                self.assert_contract_failure(
                    _request(
                        linux_manifest_digest=digest,
                        windows_manifest=windows_manifest,
                    )
                )

        windows_manifest = _windows_manifest("4" * 64)
        self.assert_contract_failure(
            _request(
                linux_manifest_digest=digest,
                windows_manifest=windows_manifest,
            )
        )

        windows_manifest = _windows_manifest(digest)
        windows_manifest["provenance"]["source_revision"] = "5" * 40
        self.assert_contract_failure(
            _request(
                linux_manifest_digest=digest,
                windows_manifest=windows_manifest,
            )
        )

    def test_windows_seed_lineage_matches_the_checked_manifest_contract(self):
        checked_manifest = json.loads(
            (REPO_ROOT / WINDOWS_MANIFEST_PATH).read_text(encoding="utf-8")
        )
        digest = "3" * 64
        windows_manifest = _windows_manifest(digest)
        self.assertEqual(
            windows_manifest["provenance"]["producer_lineage"],
            checked_manifest["provenance"]["producer_lineage"],
        )
        result = self.run_request(
            _request(
                linux_manifest_digest=digest,
                windows_manifest=windows_manifest,
            )
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_windows_seed_requires_exact_six_name_inventory_and_producers(self):
        digest = "3" * 64
        windows_manifest = _windows_manifest(digest)
        windows_manifest["artifacts"].pop()
        self.assert_contract_failure(
            _request(
                linux_manifest_digest=digest,
                windows_manifest=windows_manifest,
            )
        )

        windows_manifest = _windows_manifest(digest)
        windows_manifest["artifacts"][-1] = dict(
            windows_manifest["artifacts"][0]
        )
        self.assert_contract_failure(
            _request(
                linux_manifest_digest=digest,
                windows_manifest=windows_manifest,
            )
        )

        windows_manifest = _windows_manifest(digest)
        windows_manifest["artifacts"][-1]["name"] = "other"
        windows_manifest["artifacts"][-1]["file"] = "other.exe"
        self.assert_contract_failure(
            _request(
                linux_manifest_digest=digest,
                windows_manifest=windows_manifest,
            )
        )

        windows_manifest = _windows_manifest(digest)
        windows_manifest["artifacts"][0]["producer"] = False
        self.assert_contract_failure(
            _request(
                linux_manifest_digest=digest,
                windows_manifest=windows_manifest,
            )
        )

    def test_windows_seed_sizes_and_digests_match_observations_and_policy(self):
        digest = "3" * 64
        windows_manifest = _windows_manifest(digest)
        windows_observations = _windows_observations(windows_manifest)
        path, kind, size, artifact_digest = windows_observations[0]
        windows_observations[0] = (path, kind, size + 1, artifact_digest)
        self.assert_contract_failure(
            _request(
                linux_manifest_digest=digest,
                windows_manifest=windows_manifest,
                windows_observations=windows_observations,
            )
        )

        windows_observations = _windows_observations(windows_manifest)
        path, kind, size, _artifact_digest = windows_observations[0]
        windows_observations[0] = (path, kind, size, "6" * 64)
        self.assert_contract_failure(
            _request(
                linux_manifest_digest=digest,
                windows_manifest=windows_manifest,
                windows_observations=windows_observations,
            )
        )

        windows_manifest["artifacts"][0]["size"] += 1
        windows_observations = _windows_observations(windows_manifest)
        self.assert_contract_failure(
            _request(
                linux_manifest_digest=digest,
                windows_manifest=windows_manifest,
                windows_observations=windows_observations,
            )
        )

    def test_windows_seed_observations_reject_duplicate_missing_and_unsafe(self):
        digest = "3" * 64
        windows_manifest = _windows_manifest(digest)
        windows_observations = _windows_observations(windows_manifest)
        duplicate = windows_observations[:-1] + [windows_observations[0]]
        self.assert_contract_failure(
            _request(
                linux_manifest_digest=digest,
                windows_manifest=windows_manifest,
                windows_observations=duplicate,
            )
        )
        self.assert_contract_failure(
            _request(
                linux_manifest_digest=digest,
                windows_manifest=windows_manifest,
                windows_observations=windows_observations[:-1],
            )
        )
        path, kind, size, artifact_digest = windows_observations[0]
        windows_observations[0] = (
            "bootstrap/seeds/i386-windows/../cupidasm.exe",
            kind,
            size,
            artifact_digest,
        )
        self.assert_contract_failure(
            _request(
                linux_manifest_digest=digest,
                windows_manifest=windows_manifest,
                windows_observations=windows_observations,
            )
        )
        self.assert_contract_failure(
            _request(windows_manifest_path="../manifest.json")
        )

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

    def test_seed_manifest_requires_six_named_elf_artifacts_and_producers(self):
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

        manifest = _manifest()
        build = next(
            artifact
            for artifact in manifest["artifacts"]
            if artifact["name"] == "cupidbuild"
        )
        build["producer"] = True
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
        self.assert_contract_failure(
            _request(windows_manifest_bytes=b'{"schema":')
        )
        windows_manifest = _windows_manifest("3" * 64)
        self.assert_contract_failure(
            _request(
                linux_manifest_digest="3" * 64,
                windows_manifest=windows_manifest,
                windows_manifest_bytes=(
                    _json_bytes(windows_manifest) + b" trailing"
                ),
            )
        )

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

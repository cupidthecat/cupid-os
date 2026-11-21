## @file
#  @brief Verify the checked toolchain manifest reader and author.
import copy
import json
import os
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tests.toolchain_manifest_profile_cases import ManifestProfileCases
from tests.toolchain_manifest_fixtures import (
    BOOTSTRAP_OBJECT_NAMES,
    BOOTSTRAP_PATHS,
    BOOTSTRAP_TOOL_NAMES,
    INPUT_PATHS,
    OBJECT_COMPARISON_NAMES,
    REPO_ROOT,
    _append_bytes,
    _author_request,
    _build_contract,
    _captured_seed_fixture,
    _context_request,
    _digest,
    _fixture,
    _json_bytes,
    _matching_bootstrap_pairs,
    _matching_executable_pairs,
    _matching_object_pairs,
    _profile_fixture,
    _request,
    _seed_fixture,
)
from tools import cupidc_toolchain_contracts


class ToolchainManifestContractTests(ManifestProfileCases, unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.contract_build = tempfile.TemporaryDirectory(
            prefix=".toolchain-manifest-contract-"
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

    def run_author_request(self, payload):
        self.request_path.write_bytes(payload)
        return subprocess.run(
            [self.contract, "author", self.request_path],
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
            result.stderr.startswith(
                "Cupid Toolchain manifest contract failed:"
            ),
            result.stderr,
        )

    def assert_author_failure(self, payload):
        result = self.run_author_request(payload)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertEqual(result.stdout, "")
        self.assertTrue(
            result.stderr.startswith(
                "Cupid Toolchain manifest contract failed:"
            ),
            result.stderr,
        )

    def test_valid_snapshot_emits_canonical_report(self):
        manifest, observations = _fixture()
        result = self.run_request(
            _request(manifest=manifest, observations=observations)
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            '{"artifact_count":22,"artifact_total_bytes":682,'
            '"bootstrap_source_input_count":80,"input_count":92,'
            '"schema":"cupid.toolchain-manifest-verification.v1"}\n',
        )
        self.assertEqual(result.stderr, "")

    def test_author_emits_the_exact_canonical_manifest_from_facts(self):
        manifest, observations = _fixture()

        result = self.run_author_request(
            _author_request(manifest=manifest, observations=observations)
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout.encode("ascii"),
            (
                json.dumps(manifest, indent=2, sort_keys=True) + "\n"
            ).encode("ascii"),
        )
        self.assertEqual(result.stderr, "")

    def test_captured_seed_author_accepts_candidate_plan_without_manifest_pins(self):
        seed_bytes, observations = _captured_seed_fixture()
        result = self.run_author_request(_context_request("author", seed_bytes=seed_bytes, observations=observations))
        self.assertEqual((result.returncode, result.stderr), (0, ""))
        manifest = json.loads(result.stdout)
        self.assertEqual(manifest["bootstrap"]["seed_manifest"]["sha256"], _digest(seed_bytes))
        self.assertEqual(manifest["tool_fixed_point"]["c_objects"], 29)

    def test_captured_seed_verifier_accepts_candidate_plan_without_manifest_pins(self):
        result = self.run_request(_context_request("check"))
        self.assertEqual((result.returncode, result.stderr), (0, ""))
        self.assertEqual(json.loads(result.stdout)["artifact_count"], 22)

    def test_author_hashes_matching_stage_object_pairs(self):
        manifest, observations = _fixture()
        object_pairs = _matching_object_pairs(manifest)

        result = self.run_author_request(
            _author_request(
                manifest=manifest,
                observations=observations,
                object_pairs=object_pairs,
            )
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        )
        self.assertEqual(result.stderr, "")

    def test_author_rejects_mismatched_stage_object_bytes(self):
        manifest, observations = _fixture()
        object_pairs = _matching_object_pairs(manifest)
        name, first_kind, first_bytes, second_kind, second_bytes = (
            object_pairs[0]
        )
        object_pairs[0] = (
            name,
            first_kind,
            first_bytes,
            second_kind,
            bytes([second_bytes[0] + 1]),
        )

        self.assert_author_failure(
            _author_request(
                manifest=manifest,
                observations=observations,
                object_pairs=object_pairs,
            )
        )

    def test_author_rejects_truncated_stage_object_bytes(self):
        manifest, observations = _fixture()
        object_pairs = _matching_object_pairs(manifest)
        payload = _author_request(
            manifest=manifest,
            observations=observations,
            object_pairs=object_pairs,
        )
        name, first_kind, first_bytes, second_kind, second_bytes = (
            object_pairs[0]
        )
        record = bytearray()
        _append_bytes(record, name.encode("ascii"))
        record.extend(struct.pack("<I", first_kind))
        _append_bytes(record, first_bytes)
        record.extend(struct.pack("<I", second_kind))
        _append_bytes(record, second_bytes)
        record_offset = payload.index(bytes(record))
        truncated = payload[: record_offset + len(record) - 1]

        self.assert_author_failure(truncated)

    def test_author_rejects_nonregular_stage_object_kind(self):
        manifest, observations = _fixture()
        object_pairs = _matching_object_pairs(manifest)
        name, _first_kind, first_bytes, second_kind, second_bytes = (
            object_pairs[0]
        )
        object_pairs[0] = (
            name,
            2,
            first_bytes,
            second_kind,
            second_bytes,
        )

        self.assert_author_failure(
            _author_request(
                manifest=manifest,
                observations=observations,
                object_pairs=object_pairs,
            )
        )

    def test_author_rejects_duplicate_stage_object_pair(self):
        manifest, observations = _fixture()
        object_pairs = _matching_object_pairs(manifest)
        object_pairs[-1] = object_pairs[0]

        self.assert_author_failure(
            _author_request(
                manifest=manifest,
                observations=observations,
                object_pairs=object_pairs,
            )
        )

    def test_author_recovers_after_rejected_stage_object_pair(self):
        manifest, observations = _fixture()
        object_pairs = _matching_object_pairs(manifest)
        rejected_pairs = list(object_pairs)
        name, first_kind, first_bytes, second_kind, _second_bytes = (
            rejected_pairs[0]
        )
        rejected_pairs[0] = (
            name,
            first_kind,
            first_bytes,
            second_kind,
            b"different",
        )
        self.assert_author_failure(
            _author_request(
                manifest=manifest,
                observations=observations,
                object_pairs=rejected_pairs,
            )
        )

        result = self.run_author_request(
            _author_request(
                manifest=manifest,
                observations=observations,
                object_pairs=object_pairs,
            )
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        )
        self.assertEqual(result.stderr, "")

    def test_author_decides_all_sixty_nine_stage_pairs(self):
        manifest, observations = _fixture()
        object_pairs = _matching_object_pairs(manifest)
        executable_pairs = _matching_executable_pairs(manifest)
        bootstrap_object_pairs = _matching_bootstrap_pairs(
            BOOTSTRAP_OBJECT_NAMES, "bootstrap-object"
        )
        bootstrap_tool_pairs = _matching_bootstrap_pairs(
            BOOTSTRAP_TOOL_NAMES, "bootstrap-tool"
        )

        result = self.run_author_request(
            _author_request(
                manifest=manifest,
                observations=observations,
                object_pairs=object_pairs,
                executable_pairs=executable_pairs,
                bootstrap_object_pairs=bootstrap_object_pairs,
                bootstrap_tool_pairs=bootstrap_tool_pairs,
            )
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        )
        self.assertEqual(
            sum(
                len(pairs)
                for pairs in (
                    object_pairs,
                    executable_pairs,
                    bootstrap_object_pairs,
                    bootstrap_tool_pairs,
                )
            ),
            69,
        )

    def test_author_rejects_mismatch_in_each_remaining_pair_lane(self):
        for lane in (
            "executable_pairs",
            "bootstrap_object_pairs",
            "bootstrap_tool_pairs",
        ):
            with self.subTest(lane=lane):
                manifest, observations = _fixture()
                pairs_by_lane = {
                    "executable_pairs": _matching_executable_pairs(
                        manifest
                    ),
                    "bootstrap_object_pairs": _matching_bootstrap_pairs(
                        BOOTSTRAP_OBJECT_NAMES, "bootstrap-object"
                    ),
                    "bootstrap_tool_pairs": _matching_bootstrap_pairs(
                        BOOTSTRAP_TOOL_NAMES, "bootstrap-tool"
                    ),
                }
                changed = list(pairs_by_lane[lane])
                (
                    name,
                    first_kind,
                    first_bytes,
                    second_kind,
                    second_bytes,
                ) = changed[0]
                changed[0] = (
                    name,
                    first_kind,
                    first_bytes,
                    second_kind,
                    second_bytes[:-1] + bytes([second_bytes[-1] ^ 1]),
                )
                pairs_by_lane[lane] = changed

                self.assert_author_failure(
                    _author_request(
                        manifest=manifest,
                        observations=observations,
                        **pairs_by_lane,
                    )
                )

    def test_author_rejects_matching_executable_pair_with_wrong_artifact_fact(
        self,
    ):
        manifest, observations = _fixture()
        executable_pairs = _matching_executable_pairs(manifest)
        name, first_kind, _first_bytes, second_kind, _second_bytes = (
            executable_pairs[0]
        )
        changed_bytes = b"equal but not the published artifact"
        executable_pairs[0] = (
            name,
            first_kind,
            changed_bytes,
            second_kind,
            changed_bytes,
        )

        self.assert_author_failure(
            _author_request(
                manifest=manifest,
                observations=observations,
                executable_pairs=executable_pairs,
            )
        )

    def test_author_rejects_mismatched_stage_object_sizes(self):
        manifest, observations = _fixture()
        object_pairs = _matching_object_pairs(manifest)
        name, first_kind, first_bytes, second_kind, second_bytes = (
            object_pairs[0]
        )
        object_pairs[0] = (
            name,
            first_kind,
            first_bytes,
            second_kind,
            second_bytes + b"x",
        )

        self.assert_author_failure(
            _author_request(
                manifest=manifest,
                observations=observations,
                object_pairs=object_pairs,
            )
        )

    @unittest.skipIf(
        os.name == "nt" and shutil.which("wsl") is None,
        "WSL is required to execute the checked Linux stage-four seed",
    )
    def test_checked_stage_four_author_builds_and_emits_oracle_bytes(self):
        seed_manifest = (
            REPO_ROOT / "bootstrap/seeds/i386-linux/manifest.json"
        )
        with tempfile.TemporaryDirectory(
            prefix=".cupman4-checked-stage-four-", dir=REPO_ROOT
        ) as temporary:
            workspace = Path(temporary)
            stage_four = workspace / "stage-four"
            seed = cupidc_toolchain_contracts.freeze_seed_inputs(
                seed_manifest, stage_four
            )
            self.assertEqual(
                seed.manifest["provenance"]["seed_generation"],
                "stage-four",
            )
            runner = cupidc_toolchain_contracts.ToolRunner(REPO_ROOT)
            for name, logical_source, gnu_extensions in (
                ("ctool", "toolchain/ctool.cc", False),
                ("ctool_host", "toolchain/ctool_host.cc", False),
                (
                    "runtime",
                    "toolchain/hosted/i386-linux/runtime.cc",
                    True,
                ),
            ):
                output = stage_four / f"{name}.o"
                arguments = ["--root", REPO_ROOT]
                if gnu_extensions:
                    arguments.append("--gnu")
                arguments.extend(
                    (
                        "-c",
                        f"/{logical_source}",
                        "-I",
                        "/toolchain",
                        "--include-angle",
                        "/toolchain/hosted/i386-linux/include",
                        "-o",
                        "/" + output.relative_to(REPO_ROOT).as_posix(),
                    )
                )
                result = runner.run(
                    seed.tools["cupidc"], tuple(arguments), 300
                )
                self.assertEqual(
                    (result.returncode, result.stdout, result.stderr),
                    (0, "", ""),
                    f"checked stage-four CupidC failed for {logical_source}",
                )
                cupidc_toolchain_contracts._validate_i386_relocatable(
                    output
                )

            executable = cupidc_toolchain_contracts._build_manifest_author(
                REPO_ROOT,
                stage_four,
                workspace / "author-build",
            )
            if os.name == "nt":
                self.assertEqual(executable.suffix, ".exe")
                cupidc_toolchain_contracts._validate_static_i386_pe32(
                    executable,
                    int(
                        cupidc_toolchain_contracts.EXPECTED_WINDOWS_TARGET[
                            "entry"
                        ]
                    ),
                    cupidc_toolchain_contracts.WINDOWS_TOOL_IMPORTS,
                )
            manifest, observations = _fixture()
            request = workspace / "request.bin"
            request.write_bytes(
                _author_request(
                    manifest=manifest,
                    observations=observations,
                )
            )
            native_commands: list[tuple[str, ...]] = []
            real_subprocess_run = subprocess.run

            def run_without_wsl(command, *arguments, **keywords):
                executable_name = Path(str(command[0])).name.lower()
                if executable_name in {"wsl", "wsl.exe"}:
                    raise AssertionError(
                        "the native PE author crossed the WSL boundary"
                    )
                native_commands.append(tuple(str(item) for item in command))
                return real_subprocess_run(command, *arguments, **keywords)

            with mock.patch.object(
                cupidc_toolchain_contracts.subprocess,
                "run",
                side_effect=run_without_wsl,
            ):
                result = runner.run(executable, ("author", request), 120)
            cupidc_toolchain_contracts.require_live_seed_inputs(seed)

            self.assertEqual(len(native_commands), 1)
            self.assertEqual(
                Path(native_commands[0][0]).suffix,
                ".exe" if os.name == "nt" else ".elf",
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stderr, "")
            self.assertEqual(
                result.stdout.encode("ascii"),
                (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode(
                    "ascii"
                ),
            )

            for aliases, long_paths in ((False, True), (True, False), (True, True)):
                with self.subTest(aliases=aliases, long_paths=long_paths):
                    manifest, observations = _profile_fixture(
                        user_link_aliases=aliases, long_paths=long_paths
                    )

                    def execute(mode, payload):
                        request.write_bytes(payload)
                        with mock.patch.object(
                            cupidc_toolchain_contracts.subprocess,
                            "run",
                            side_effect=run_without_wsl,
                        ):
                            return runner.run(executable, (mode, request), 120)

                    author_payload = _author_request(
                        manifest=manifest, observations=observations
                    )
                    canonical = json.dumps(manifest, indent=2, sort_keys=True) + "\n"
                    authored = execute("author", author_payload)
                    self.assertEqual(authored.returncode, 0, authored.stderr)
                    self.assertEqual(authored.stdout, canonical)
                    checked = execute(
                        "check", _request(manifest=manifest, observations=observations)
                    )
                    self.assertEqual(checked.returncode, 0, checked.stderr)
                    self.assertEqual(
                        json.loads(checked.stdout)["bootstrap_source_input_count"],
                        manifest["bootstrap"]["source_inputs"]["count"],
                    )
                    if aliases:
                        for removed in (
                            "toolchain/hosted/i386-windows/final_path_start.asm",
                            BOOTSTRAP_PATHS[0],
                        ):
                            bad = copy.deepcopy(manifest)
                            inputs = bad["bootstrap"]["source_inputs"]
                            inputs["files"]["toolchain/unexpected.asm"] = inputs["files"].pop(removed)
                            inputs["sha256"] = _digest(_json_bytes(inputs["files"]))
                            for mode, framing in (("author", _author_request), ("check", _request)):
                                rejected = execute(mode, framing(manifest=bad, observations=observations))
                                self.assertEqual(rejected.returncode, 1, rejected.stderr)
                                self.assertEqual(rejected.stdout, "")
                        sources = manifest["bootstrap"]["source_inputs"]["files"]
                        duplicate = [
                            (path, 1, row["size"], row["sha256"])
                            for path, row in sorted(sources.items())
                        ]
                        duplicate[0] = duplicate[-1]
                        for mode, framing in (("author", _author_request), ("check", _request)):
                            rejected = execute(
                                mode, framing(manifest=manifest, observations=observations,
                                              bootstrap_observations=duplicate)
                            )
                            self.assertEqual(rejected.returncode, 1, rejected.stderr)
                            self.assertEqual(rejected.stdout, "")
                        recovered = execute(
                            "author", _author_request(manifest=manifest, observations=observations)
                        )
                        self.assertEqual(recovered.returncode, 0, recovered.stderr)
                        self.assertEqual(recovered.stdout, canonical)
            cupidc_toolchain_contracts.require_live_seed_inputs(seed)
            self.assertTrue(all(
                Path(command[0]).suffix == (".exe" if os.name == "nt" else ".elf")
                for command in native_commands
            ))

    def test_author_output_is_independent_of_fact_order(self):
        manifest, observations = _fixture()
        input_observations = [
            (path, 1, record["size"], record["sha256"])
            for path, record in sorted(manifest["inputs"].items())
        ]
        bootstrap_observations = [
            (path, 1, record["size"], record["sha256"])
            for path, record in sorted(
                manifest["bootstrap"]["source_inputs"]["files"].items()
            )
        ]
        seed_bytes, seed_observations = _seed_fixture(manifest)
        object_pairs = _matching_object_pairs(manifest)
        executable_pairs = _matching_executable_pairs(manifest)
        bootstrap_object_pairs = _matching_bootstrap_pairs(
            BOOTSTRAP_OBJECT_NAMES, "bootstrap-object"
        )
        bootstrap_tool_pairs = _matching_bootstrap_pairs(
            BOOTSTRAP_TOOL_NAMES, "bootstrap-tool"
        )

        result = self.run_author_request(
            _author_request(
                manifest=manifest,
                observations=list(reversed(observations)),
                input_observations=list(reversed(input_observations)),
                bootstrap_observations=list(
                    reversed(bootstrap_observations)
                ),
                seed_manifest_bytes=seed_bytes,
                seed_observations=list(reversed(seed_observations)),
                object_pairs=list(reversed(object_pairs)),
                executable_pairs=list(reversed(executable_pairs)),
                bootstrap_object_pairs=list(
                    reversed(bootstrap_object_pairs)
                ),
                bootstrap_tool_pairs=list(reversed(bootstrap_tool_pairs)),
            )
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        )

    def test_publisher_framing_drives_the_standalone_author(self):
        manifest, artifact_observations = _fixture()
        seed_bytes, seed_observations = _seed_fixture(manifest)
        request = cupidc_toolchain_contracts._manifest_author_request(
            artifact_observations,
            [
                (path, 1, record["size"], record["sha256"])
                for path, record in sorted(manifest["inputs"].items())
            ],
            [
                (path, 1, record["size"], record["sha256"])
                for path, record in sorted(
                    manifest["bootstrap"]["source_inputs"]["files"].items()
                )
            ],
            manifest["bootstrap"]["source_inputs"]["sha256"],
            manifest["bootstrap"]["seed_manifest"]["path"],
            seed_bytes,
            seed_observations,
            _matching_object_pairs(manifest),
            _matching_executable_pairs(manifest),
            _matching_bootstrap_pairs(
                BOOTSTRAP_OBJECT_NAMES, "bootstrap-object"
            ),
            _matching_bootstrap_pairs(
                BOOTSTRAP_TOOL_NAMES, "bootstrap-tool"
            ),
        )

        result = self.run_author_request(request)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        )

    def test_author_framing_rejects_truncation_and_trailing_bytes(self):
        payload = _author_request()
        self.assert_author_failure(payload[:-1])
        self.assert_author_failure(payload + b"x")

    def test_author_protocol_has_no_caller_all_equal_field(self):
        payload = _author_request()

        result = self.run_author_request(payload)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assert_author_failure(payload + struct.pack("<I", 1))
        self.assert_author_failure(b"CUPMAN2\0" + payload[8:])

    def test_author_rejects_substituted_and_duplicate_fact_paths(self):
        manifest, observations = _fixture()
        input_observations = [
            (path, 1, record["size"], record["sha256"])
            for path, record in sorted(manifest["inputs"].items())
        ]
        substituted = list(input_observations)
        _path, kind, size, digest = substituted[0]
        substituted[0] = (
            "toolchain/tests/not-the-author.cc",
            kind,
            size,
            digest,
        )
        self.assert_author_failure(
            _author_request(
                manifest=manifest,
                observations=observations,
                input_observations=substituted,
            )
        )

        duplicated = list(input_observations)
        duplicated[-1] = duplicated[0]
        self.assert_author_failure(
            _author_request(
                manifest=manifest,
                observations=observations,
                input_observations=duplicated,
            )
        )

    def test_author_artifact_reader_rejects_wrong_missing_and_extra_facts(self):
        manifest, observations = _fixture()
        wrong = list(observations)
        _name, kind, size, digest = wrong[0]
        wrong[0] = ("not-an-artifact.elf", kind, size, digest)
        cases = {
            "wrong": wrong,
            "missing": observations[:-1],
            "extra": [*observations, observations[0]],
        }

        for name, changed in cases.items():
            with self.subTest(name=name):
                self.assert_author_failure(
                    _author_request(
                        manifest=manifest,
                        observations=changed,
                    )
                )

    def test_author_bootstrap_reader_rejects_each_corrupt_fact_class(self):
        manifest, observations = _fixture()
        bootstrap = [
            (path, 1, record["size"], record["sha256"])
            for path, record in sorted(
                manifest["bootstrap"]["source_inputs"]["files"].items()
            )
        ]
        wrong_path = list(bootstrap)
        _path, kind, size, digest = wrong_path[0]
        wrong_path[0] = ("toolchain/not-a-bootstrap-source.cc", kind, size, digest)
        wrong_size = list(bootstrap)
        path, kind, size, digest = wrong_size[0]
        wrong_size[0] = (path, kind, size + 1, digest)
        wrong_digest = list(bootstrap)
        path, kind, size, _digest_value = wrong_digest[0]
        wrong_digest[0] = (path, kind, size, "0" * 64)
        cases = {
            "count": bootstrap[:-1],
            "path": wrong_path,
            "size": wrong_size,
            "digest": wrong_digest,
        }

        for name, changed in cases.items():
            with self.subTest(name=name):
                self.assert_author_failure(
                    _author_request(
                        manifest=manifest,
                        observations=observations,
                        bootstrap_observations=changed,
                    )
                )

    def test_author_seed_reader_rejects_each_corrupt_fact_class(self):
        manifest, observations = _fixture()
        seed_bytes, seed_observations = _seed_fixture(manifest)
        wrong_size = list(seed_observations)
        name, kind, size, digest = wrong_size[0]
        wrong_size[0] = (name, kind, size + 1, digest)
        wrong_digest = list(seed_observations)
        name, kind, size, _digest_value = wrong_digest[0]
        wrong_digest[0] = (name, kind, size, "0" * 64)
        cases = {
            "path": {
                "seed_manifest_path": "bootstrap/seeds/not-linux/manifest.json",
                "seed_manifest_bytes": seed_bytes,
                "seed_observations": seed_observations,
            },
            "bytes": {
                "seed_manifest_bytes": seed_bytes + b"x",
                "seed_observations": seed_observations,
            },
            "artifact size": {
                "seed_manifest_bytes": seed_bytes,
                "seed_observations": wrong_size,
            },
            "artifact digest": {
                "seed_manifest_bytes": seed_bytes,
                "seed_observations": wrong_digest,
            },
        }

        for name, changed in cases.items():
            with self.subTest(name=name):
                self.assert_author_failure(
                    _author_request(
                        manifest=manifest,
                        observations=observations,
                        **changed,
                    )
                )

    def test_author_object_reader_rejects_missing_duplicate_and_empty_pairs(self):
        manifest, observations = _fixture()
        objects = _matching_object_pairs(manifest)
        duplicated = list(objects)
        duplicated[-1] = duplicated[0]
        empty = list(objects)
        name, first_kind, _first_bytes, second_kind, _second_bytes = empty[0]
        empty[0] = (name, first_kind, b"", second_kind, b"")
        cases = {
            "missing": objects[:-1],
            "duplicate": duplicated,
            "empty": empty,
        }

        for name, changed in cases.items():
            with self.subTest(name=name):
                self.assert_author_failure(
                    _author_request(
                        manifest=manifest,
                        observations=observations,
                        object_pairs=changed,
                    )
                )

    def test_author_output_binds_input_and_object_sizes(self):
        manifest, observations = _fixture()
        inputs = [
            (path, 1, record["size"] + 101, record["sha256"])
            for path, record in sorted(manifest["inputs"].items())
        ]
        objects = _matching_object_pairs(manifest)

        baseline = self.run_author_request(
            _author_request(
                manifest=manifest,
                observations=observations,
                input_observations=inputs,
                object_pairs=objects,
            )
        )
        self.assertEqual(baseline.returncode, 0, baseline.stderr)

        changed_inputs = list(inputs)
        path, kind, size, digest = changed_inputs[0]
        changed_inputs[0] = (path, kind, size + 1, digest)
        changed_input = self.run_author_request(
            _author_request(
                manifest=manifest,
                observations=observations,
                input_observations=changed_inputs,
                object_pairs=objects,
            )
        )
        self.assertEqual(changed_input.returncode, 0, changed_input.stderr)
        self.assertNotEqual(changed_input.stdout, baseline.stdout)

        changed_objects = list(objects)
        name, first_kind, first_bytes, second_kind, second_bytes = (
            changed_objects[0]
        )
        changed_bytes = first_bytes + b"x"
        changed_objects[0] = (
            name,
            first_kind,
            changed_bytes,
            second_kind,
            changed_bytes,
        )
        changed_object = self.run_author_request(
            _author_request(
                manifest=manifest,
                observations=observations,
                input_observations=inputs,
                object_pairs=changed_objects,
            )
        )
        self.assertEqual(changed_object.returncode, 0, changed_object.stderr)
        self.assertNotEqual(changed_object.stdout, baseline.stdout)

    def test_author_derives_fixed_point_summary_from_pair_inventories(self):
        manifest, observations = _fixture()
        manifest["tool_fixed_point"] = {
            "all_equal": False,
            "c_objects": 18,
            "compared_generations": ["stage-two"],
            "startup_objects": 2,
            "tool_images": 4,
        }

        result = self.run_author_request(
            _author_request(manifest=manifest, observations=observations)
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            json.loads(result.stdout)["tool_fixed_point"],
            {
                "all_equal": True,
                "c_objects": 29,
                "compared_generations": ["stage-three", "stage-four"],
                "startup_objects": 1,
                "tool_images": 6,
            },
        )

    def test_manifest_object_order_does_not_change_the_result(self):
        manifest, observations = _fixture()
        reordered = {"comparisons": manifest["comparisons"]}
        reordered.update(
            (key, value)
            for key, value in manifest.items()
            if key != "comparisons"
        )
        manifest_bytes = json.dumps(
            reordered,
            ensure_ascii=True,
            separators=(",", ":"),
        ).encode("ascii")
        result = self.run_request(
            _request(
                manifest=manifest,
                manifest_bytes=manifest_bytes,
                observations=observations,
            )
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_request_framing_rejects_truncation_and_trailing_bytes(self):
        payload = _request()
        self.assert_contract_failure(payload[:-1])
        self.assert_contract_failure(payload + b"x")
        self.assert_contract_failure(b"CUPMAN1\0" + payload[8:])

    def test_live_input_observations_are_independent_evidence(self):
        manifest, observations = _fixture()
        live = [
            (path, 1, record["size"], record["sha256"])
            for path, record in sorted(manifest["inputs"].items())
        ]
        for field, replacement in (
            (1, 2),
            (3, "0" * 64),
            (0, "toolchain/not-the-captured-input.h"),
        ):
            with self.subTest(field=field):
                changed = list(live)
                record = list(changed[0])
                record[field] = replacement
                changed[0] = tuple(record)
                self.assert_contract_failure(
                    _request(
                        manifest=manifest,
                        observations=observations,
                        input_observations=changed,
                    )
                )
        changed_size = list(live)
        path, kind, size, digest = changed_size[1]
        changed_size[1] = (path, kind, size + 1, digest)
        self.assert_contract_failure(
            _request(
                manifest=manifest,
                observations=observations,
                input_observations=changed_size,
            )
        )
        duplicate = list(live)
        duplicate[-1] = duplicate[0]
        self.assert_contract_failure(
            _request(
                manifest=manifest,
                observations=observations,
                input_observations=duplicate,
            )
        )

    def test_bootstrap_observations_are_independent_evidence(self):
        manifest, observations = _fixture()
        files = manifest["bootstrap"]["source_inputs"]["files"]
        live = [
            (path, 1, record["size"], record["sha256"])
            for path, record in sorted(files.items())
        ]
        for field, replacement in (
            (1, 2),
            (2, live[0][2] + 1),
            (3, "0" * 64),
            (0, "toolchain/not-the-captured-source.cc"),
        ):
            with self.subTest(field=field):
                changed = list(live)
                record = list(changed[0])
                record[field] = replacement
                changed[0] = tuple(record)
                self.assert_contract_failure(
                    _request(
                        manifest=manifest,
                        observations=observations,
                        bootstrap_observations=changed,
                    )
                )

    def test_seed_observations_are_independent_evidence(self):
        manifest, observations = _fixture()
        seed_bytes, live = _seed_fixture(manifest)
        for field, replacement in (
            (1, 2),
            (2, live[0][2] + 1),
            (3, "0" * 64),
            (0, "not-the-captured-seed.elf"),
        ):
            with self.subTest(field=field):
                changed = list(live)
                record = list(changed[0])
                record[field] = replacement
                changed[0] = tuple(record)
                self.assert_contract_failure(
                    _request(
                        manifest=manifest,
                        observations=observations,
                        seed_manifest_bytes=seed_bytes,
                        seed_observations=changed,
                    )
                )
        duplicate = list(live)
        duplicate[-1] = duplicate[0]
        self.assert_contract_failure(
            _request(
                manifest=manifest,
                observations=observations,
                seed_manifest_bytes=seed_bytes,
                seed_observations=duplicate,
            )
        )
        self.assert_contract_failure(
            _request(
                manifest=manifest,
                observations=observations,
                seed_manifest_path="bootstrap/seeds/other/manifest.json",
                seed_manifest_bytes=seed_bytes,
                seed_observations=live,
            )
        )
        changed_seed = bytearray(seed_bytes)
        changed_seed[-2] ^= 1
        self.assert_contract_failure(
            _request(
                manifest=manifest,
                observations=observations,
                seed_manifest_bytes=bytes(changed_seed),
                seed_observations=live,
            )
        )

    def test_duplicate_manifest_keys_are_rejected(self):
        manifest, observations = _fixture()
        manifest_bytes = _json_bytes(manifest).replace(
            b'"status":"pass"',
            b'"status":"pass","status":"pass"',
            1,
        )
        self.assert_contract_failure(
            _request(
                manifest=manifest,
                manifest_bytes=manifest_bytes,
                observations=observations,
            )
        )

    def test_malformed_object_comparison_record_is_rejected(self):
        manifest, observations = _fixture()
        manifest_bytes = _json_bytes(manifest).replace(
            b'"object_comparisons":{"as_elf":{',
            b'"object_comparisons":{"as_elf"{',
            1,
        )
        self.assert_contract_failure(
            _request(
                manifest=manifest,
                manifest_bytes=manifest_bytes,
                observations=observations,
            )
        )

    def test_object_comparison_hashes_remain_producer_evidence(self):
        manifest, observations = _fixture()
        manifest["object_comparisons"] = {
            name: {"sha256": "0" * 64, "size": index + 1}
            for index, name in enumerate(OBJECT_COMPARISON_NAMES)
        }
        result = self.run_request(
            _request(manifest=manifest, observations=observations)
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_manifest_schema_is_exact(self):
        manifest, observations = _fixture()
        manifest["schema"] = "cupid.toolchain-contracts.v1"
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

    def test_manifest_metadata_requires_the_exact_shape(self):
        manifest, observations = _fixture()
        manifest["status"] = "unchecked"
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["target"]["entry"] = 0x08049000
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["unexpected"] = None
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        del manifest["target"]
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

    def test_tool_fixed_point_record_is_exact(self):
        manifest, observations = _fixture()
        manifest["tool_fixed_point"]["all_equal"] = False
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["tool_fixed_point"]["c_objects"] = 18
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["tool_fixed_point"]["compared_generations"].reverse()
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

    def test_artifact_records_match_the_exact_observed_cohort(self):
        manifest, observations = _fixture()
        manifest["artifacts"].pop()
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        name, kind, size, digest = observations[0]
        observations[0] = (name, kind, size + 1, digest)
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        name, kind, size, _digest_value = observations[0]
        observations[0] = (name, kind, size, "0" * 64)
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

    def test_comparison_maps_bind_the_exact_names_and_digests(self):
        manifest, observations = _fixture()
        manifest["comparisons"]["runtime"] = "0" * 64
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        del manifest["object_comparisons"]["as_elf"]
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["object_comparisons"]["as_elf"]["size"] = 0
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["object_comparisons"]["unknown"] = {
            "sha256": "1" * 64,
            "size": 1,
        }
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

    def test_input_inventory_count_paths_and_digests_are_checked(self):
        manifest, observations = _fixture()
        manifest["input_count"] += 1
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        first_path = INPUT_PATHS[0]
        manifest["inputs"][first_path]["sha256"] = "A" * 64
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["inputs"]["../outside"] = manifest["inputs"].pop(
            first_path
        )
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["inputs"]["toolchain/substituted-input.h"] = (
            manifest["inputs"].pop(first_path)
        )
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

    def test_bootstrap_record_binds_its_canonical_source_snapshot(self):
        manifest, observations = _fixture()
        empty_snapshot = _json_bytes({})
        manifest["bootstrap"]["source_inputs"] = {
            "count": 0,
            "files": {},
            "sha256": _digest(empty_snapshot),
        }
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["bootstrap"]["source_inputs"]["count"] += 1
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["bootstrap"]["source_inputs"]["sha256"] = "0" * 64
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["bootstrap"]["seed_manifest"]["path"] = "../seed.json"
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        files = manifest["bootstrap"]["source_inputs"]["files"]
        files["toolchain/substituted-source.cc"] = files.pop(
            BOOTSTRAP_PATHS[0]
        )
        manifest["bootstrap"]["source_inputs"]["sha256"] = _digest(
            _json_bytes(files)
        )
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )

        manifest, observations = _fixture()
        manifest["bootstrap"]["build_plan_sha256"] = "0" * 64
        self.assert_contract_failure(
            _request(manifest=manifest, observations=observations)
        )


if __name__ == "__main__":
    unittest.main()

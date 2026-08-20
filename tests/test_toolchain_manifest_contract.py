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
SOURCE = REPO_ROOT / "toolchain/tests/toolchain_manifest_contract.cc"
MAGIC = b"CUPMAN2\0"
CONTRACT_NAMES = (
    "core",
    "user-syscall-abi",
    "cupidc-pp",
    "cupidc-type",
    "cupidc-frontend",
    "cupidc-ir",
    "cupidc-object",
    "elf32",
    "x86",
    "cupiddis",
    "cupidasm",
    "cupidasm-demos",
    "cupidasm-kernel-elf",
    "cupidobj",
    "cupidld",
)
CONTRACT_ARTIFACTS = tuple(
    f"{name}-contract.elf" for name in CONTRACT_NAMES
)
TOOL_ARTIFACTS = tuple(
    f"cupidc-{name}.elf"
    for name in ("cupidasm", "cupiddis", "cupidld", "cupidobj", "cupidc")
)
ARTIFACT_NAMES = (
    *CONTRACT_ARTIFACTS,
    "cupidc-runtime-contract.elf",
    *TOOL_ARTIFACTS,
)
OBJECT_COMPARISON_NAMES = (
    *CONTRACT_NAMES,
    "as_elf",
    "runtime",
)
BUILD_PLAN_SHA256 = (
    "59c1231e6fc7caafde8781dd6a566fa0ece2909be606914f24a19a7bececadcc"
)
SEED_MANIFEST_SHA256 = (
    "d571125256d11dd707f661299738891edc5c1a8d3358554076875a3e0cac22d0"
)
INPUT_PATHS = (
    "kernel/core/syscall.cc",
    "kernel/core/syscall.h",
    "kernel/core/types.h",
    "kernel/fs/vfs.h",
    "kernel/lang/as_elf.cc",
    "kernel/lang/as_elf.h",
    "kernel/network/socket.h",
    "toolchain/Makefile",
    "toolchain/ctool.h",
    "toolchain/ctool_host.h",
    "toolchain/cupidasm.h",
    "toolchain/cupidc_emit.h",
    "toolchain/cupidc_frontend.h",
    "toolchain/cupidc_ir.h",
    "toolchain/cupidc_pp.h",
    "toolchain/cupidc_type.h",
    "toolchain/cupiddis.h",
    "toolchain/cupidld.h",
    "toolchain/cupidobj.h",
    "toolchain/elf32.h",
    "toolchain/hosted/i386-linux/include/cupid_host_abi.h",
    "toolchain/hosted/i386-linux/include/direct.h",
    "toolchain/hosted/i386-linux/include/errno.h",
    "toolchain/hosted/i386-linux/include/stdint.h",
    "toolchain/hosted/i386-linux/include/stdio.h",
    "toolchain/hosted/i386-linux/include/stdlib.h",
    "toolchain/hosted/i386-linux/include/string.h",
    "toolchain/hosted/i386-linux/include/unistd.h",
    "toolchain/hosted/i386-linux/include/windows.h",
    "toolchain/hosted/i386-windows/publication_runtime.cc",
    "toolchain/hosted/i386-windows/publication_start.asm",
    "toolchain/hosted/i386-windows/runtime.cc",
    "toolchain/hosted/i386-windows/start.asm",
    "toolchain/hosted/i386-windows/tool_start.asm",
    "toolchain/tests/core_contract.cc",
    "toolchain/tests/cupidasm_contract.cc",
    "toolchain/tests/cupidasm_demos_contract.cc",
    "toolchain/tests/cupidasm_kernel_elf_contract.cc",
    "toolchain/tests/cupidc_exact_decimal_literal_fixture.h",
    "toolchain/tests/cupidc_frontend_contract.cc",
    "toolchain/tests/cupidc_ir_contract.cc",
    "toolchain/tests/cupidc_kernel_simd_fixture.h",
    "toolchain/tests/cupidc_object_contract.cc",
    "toolchain/tests/cupidc_pp_active_cases.inc",
    "toolchain/tests/cupidc_pp_conditional_cases.inc",
    "toolchain/tests/cupidc_pp_contract.cc",
    "toolchain/tests/cupidc_static_long_double_arithmetic_fixture.h",
    "toolchain/tests/cupidc_static_long_double_control_fixture.h",
    "toolchain/tests/cupidc_static_long_double_integer_fixture.h",
    "toolchain/tests/cupidc_type_contract.cc",
    "toolchain/tests/cupiddis_contract.cc",
    "toolchain/tests/cupidld_contract.cc",
    "toolchain/tests/cupidobj_contract.cc",
    "toolchain/tests/elf32_contract.cc",
    "toolchain/tests/hosted_i386_runtime_contract.cc",
    "toolchain/tests/hosted_i386_windows_contract.cc",
    "toolchain/tests/hosted_i386_windows_runtime_contract.cc",
    "toolchain/tests/user_syscall_abi_contract.cc",
    "toolchain/tests/x86_active_cases.inc",
    "toolchain/tests/x86_catalogue_contract.inc",
    "toolchain/tests/x86_contract.cc",
    "toolchain/tests/x86_inline_cases.inc",
    "toolchain/x86.cc",
    "toolchain/x86.h",
    "tools/bootstrap_toolchain.py",
    "tools/cupidc_toolchain_contracts.py",
    "tools/user_syscall_abi.py",
    "user/cupid.h",
)
BOOTSTRAP_PATHS = (
    "link.ld",
    "toolchain/ctool.cc",
    "toolchain/ctool.h",
    "toolchain/ctool_host.cc",
    "toolchain/ctool_host.h",
    "toolchain/cupidasm.cc",
    "toolchain/cupidasm.h",
    "toolchain/cupidasm_main.cc",
    "toolchain/cupidc_emit.cc",
    "toolchain/cupidc_emit.h",
    "toolchain/cupidc_frontend.cc",
    "toolchain/cupidc_frontend.h",
    "toolchain/cupidc_ir.cc",
    "toolchain/cupidc_ir.h",
    "toolchain/cupidc_main.cc",
    "toolchain/cupidc_pp.cc",
    "toolchain/cupidc_pp.h",
    "toolchain/cupidc_type.cc",
    "toolchain/cupidc_type.h",
    "toolchain/cupiddis.cc",
    "toolchain/cupiddis.h",
    "toolchain/cupiddis_main.cc",
    "toolchain/cupidld.cc",
    "toolchain/cupidld.h",
    "toolchain/cupidld_main.cc",
    "toolchain/cupidobj.cc",
    "toolchain/cupidobj.h",
    "toolchain/cupidobj_main.cc",
    "toolchain/elf32.cc",
    "toolchain/elf32.h",
    "toolchain/hosted/i386-linux/include/cupid_host_abi.h",
    "toolchain/hosted/i386-linux/include/direct.h",
    "toolchain/hosted/i386-linux/include/errno.h",
    "toolchain/hosted/i386-linux/include/stdint.h",
    "toolchain/hosted/i386-linux/include/stdio.h",
    "toolchain/hosted/i386-linux/include/stdlib.h",
    "toolchain/hosted/i386-linux/include/string.h",
    "toolchain/hosted/i386-linux/include/unistd.h",
    "toolchain/hosted/i386-linux/include/windows.h",
    "toolchain/hosted/i386-linux/runtime.cc",
    "toolchain/hosted/i386-linux/start.asm",
    "toolchain/hosted/i386-windows/publication_runtime.cc",
    "toolchain/hosted/i386-windows/publication_start.asm",
    "toolchain/hosted/i386-windows/runtime.cc",
    "toolchain/hosted/i386-windows/start.asm",
    "toolchain/hosted/i386-windows/tool_start.asm",
    "toolchain/tests/hosted_i386_windows_contract.cc",
    "toolchain/tests/hosted_i386_windows_runtime_contract.cc",
    "toolchain/x86.cc",
    "toolchain/x86.h",
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
    output = build / ("toolchain-manifest-contract" + suffix)
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
            "toolchain manifest contract hosted build failed\n"
            + result.stdout
            + result.stderr
        )
    return output


def _digest(value):
    if isinstance(value, str):
        value = value.encode("ascii")
    return hashlib.sha256(value).hexdigest()


def _fixture():
    payloads = {
        name: f"checked:{name}\n".encode("ascii")
        for name in ARTIFACT_NAMES
    }
    records = [
        {
            "path": name,
            "sha256": _digest(payloads[name]),
            "size": len(payloads[name]),
        }
        for name in sorted(ARTIFACT_NAMES)
    ]
    records_by_name = {record["path"]: record for record in records}
    bootstrap_files = {
        path: {"sha256": _digest(f"source:{path}"), "size": index}
        for index, path in enumerate(BOOTSTRAP_PATHS)
    }
    bootstrap_snapshot = json.dumps(
        bootstrap_files,
        ensure_ascii=True,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("ascii")
    comparisons = {
        name: records_by_name[f"{name}-contract.elf"]["sha256"]
        for name in CONTRACT_NAMES
    }
    comparisons["runtime"] = records_by_name[
        "cupidc-runtime-contract.elf"
    ]["sha256"]
    manifest = {
        "artifacts": records,
        "bootstrap": {
            "build_plan_sha256": BUILD_PLAN_SHA256,
            "seed_manifest": {
                "path": "bootstrap/seeds/i386-linux/manifest.json",
                "sha256": SEED_MANIFEST_SHA256,
            },
            "source_inputs": {
                "count": len(bootstrap_files),
                "files": bootstrap_files,
                "sha256": _digest(bootstrap_snapshot),
            },
        },
        "comparisons": comparisons,
        "input_count": 68,
        "inputs": {path: _digest(f"input:{path}") for path in INPUT_PATHS},
        "object_comparisons": {
            name: _digest(f"object:{name}")
            for name in OBJECT_COMPARISON_NAMES
        },
        "schema": "cupid.toolchain-contracts.v2",
        "status": "pass",
        "target": {
            "architecture": "i386",
            "entry": 0x08048000,
            "linkage": "static",
            "operating_system": "linux",
        },
        "tool_fixed_point": {
            "all_equal": True,
            "c_objects": 19,
            "compared_generations": ["stage-three", "stage-four"],
            "startup_objects": 1,
            "tool_images": 5,
        },
    }
    observations = [
        (
            name,
            1,
            len(payloads[name]),
            _digest(payloads[name]),
        )
        for name in sorted(payloads)
    ]
    return manifest, observations


def _json_bytes(value):
    return json.dumps(
        value,
        ensure_ascii=True,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("ascii")


def _append_bytes(payload, value):
    payload.extend(struct.pack("<I", len(value)))
    payload.extend(value)


def _seed_fixture(manifest):
    seed_path = REPO_ROOT / "bootstrap/seeds/i386-linux/manifest.json"
    seed_bytes = seed_path.read_bytes()
    seed_manifest = json.loads(seed_bytes.decode("ascii"))
    if manifest["bootstrap"]["build_plan_sha256"] != BUILD_PLAN_SHA256:
        seed_manifest = json.loads(json.dumps(seed_manifest))
        seed_manifest["build_plan_sha256"] = manifest["bootstrap"][
            "build_plan_sha256"
        ]
        seed_bytes = _json_bytes(seed_manifest)
    observations = [
        (record["file"], 1, record["size"], record["sha256"])
        for record in sorted(
            seed_manifest["artifacts"], key=lambda record: record["file"]
        )
    ]
    return seed_bytes, observations


def _request(
    *,
    manifest=None,
    manifest_bytes=None,
    observations=None,
    input_observations=None,
    bootstrap_observations=None,
    seed_manifest_path=None,
    seed_manifest_bytes=None,
    seed_observations=None,
):
    if manifest is None or observations is None:
        fixture_manifest, fixture_observations = _fixture()
        if manifest is None:
            manifest = fixture_manifest
        if observations is None:
            observations = fixture_observations
    if input_observations is None:
        input_observations = [
            (path, 1, 0, digest)
            for path, digest in sorted(manifest["inputs"].items())
        ]
    if bootstrap_observations is None:
        bootstrap_observations = [
            (path, 1, record["size"], record["sha256"])
            for path, record in sorted(
                manifest["bootstrap"]["source_inputs"]["files"].items()
            )
        ]
    if seed_manifest_path is None:
        seed_manifest_path = manifest["bootstrap"]["seed_manifest"]["path"]
    if seed_manifest_bytes is None or seed_observations is None:
        fixture_seed, fixture_seed_observations = _seed_fixture(manifest)
        if seed_manifest_bytes is None:
            seed_manifest_bytes = fixture_seed
        if seed_observations is None:
            seed_observations = fixture_seed_observations
    if manifest_bytes is None:
        manifest_bytes = _json_bytes(manifest)
    payload = bytearray(MAGIC)
    _append_bytes(payload, manifest_bytes)
    payload.extend(struct.pack("<I", len(observations)))
    for name, kind, size, digest in observations:
        _append_bytes(payload, name.encode("ascii"))
        payload.extend(struct.pack("<IQ", kind, size))
        _append_bytes(payload, digest.encode("ascii"))
    for observation_set in (input_observations, bootstrap_observations):
        payload.extend(struct.pack("<I", len(observation_set)))
        for name, kind, size, digest in observation_set:
            _append_bytes(payload, name.encode("ascii"))
            payload.extend(struct.pack("<IQ", kind, size))
            _append_bytes(payload, digest.encode("ascii"))
    _append_bytes(payload, seed_manifest_path.encode("ascii"))
    _append_bytes(payload, seed_manifest_bytes)
    payload.extend(struct.pack("<I", len(seed_observations)))
    for name, kind, size, digest in seed_observations:
        _append_bytes(payload, name.encode("ascii"))
        payload.extend(struct.pack("<IQ", kind, size))
        _append_bytes(payload, digest.encode("ascii"))
    return bytes(payload)


class ToolchainManifestContractTests(unittest.TestCase):
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

    def test_valid_snapshot_emits_canonical_report(self):
        manifest, observations = _fixture()
        result = self.run_request(
            _request(manifest=manifest, observations=observations)
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            '{"artifact_count":21,"artifact_total_bytes":652,'
            '"bootstrap_source_input_count":50,"input_count":68,'
            '"schema":"cupid.toolchain-manifest-verification.v1"}\n',
        )
        self.assertEqual(result.stderr, "")

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
            (path, 1, 0, digest)
            for path, digest in sorted(manifest["inputs"].items())
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

    def test_object_comparison_hashes_remain_producer_evidence(self):
        manifest, observations = _fixture()
        manifest["object_comparisons"] = {
            name: "0" * 64 for name in OBJECT_COMPARISON_NAMES
        }
        result = self.run_request(
            _request(manifest=manifest, observations=observations)
        )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_current_publication_inventory_counts_are_exact(self):
        for input_count in (67, 69):
            with self.subTest(input_count=input_count):
                manifest, observations = _fixture()
                manifest["inputs"] = {
                    path: _digest(f"input:{path}")
                    for path in INPUT_PATHS[:input_count]
                }
                if input_count > len(INPUT_PATHS):
                    manifest["inputs"]["toolchain/unexpected.h"] = _digest(
                        "unexpected"
                    )
                manifest["input_count"] = input_count
                self.assert_contract_failure(
                    _request(manifest=manifest, observations=observations)
                )

        for source_count in (49, 51):
            with self.subTest(source_count=source_count):
                manifest, observations = _fixture()
                bootstrap_files = {
                    path: {
                        "sha256": _digest(f"source:{path}"),
                        "size": index,
                    }
                    for index, path in enumerate(
                        BOOTSTRAP_PATHS[:source_count]
                    )
                }
                if source_count > len(BOOTSTRAP_PATHS):
                    bootstrap_files["toolchain/unexpected.cc"] = {
                        "sha256": _digest("unexpected"),
                        "size": source_count - 1,
                    }
                manifest["bootstrap"]["source_inputs"] = {
                    "count": source_count,
                    "files": bootstrap_files,
                    "sha256": _digest(_json_bytes(bootstrap_files)),
                }
                self.assert_contract_failure(
                    _request(manifest=manifest, observations=observations)
                )

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
        manifest["object_comparisons"]["unknown"] = "1" * 64
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
        manifest["inputs"][first_path] = "A" * 64
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

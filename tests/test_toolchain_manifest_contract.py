import hashlib
import json
import os
import shutil
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools import cupidc_toolchain_contracts


REPO_ROOT = Path(__file__).resolve().parents[1]
SOURCE = REPO_ROOT / "toolchain/tests/toolchain_manifest_contract.cc"
MAGIC = b"CUPMAN2\0"
AUTHOR_MAGIC = b"CUPMAN3\0"
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
    "51c8244aa51fce8ccaf7f2eb24df848f02d9269109599cdbdfb0f1f699b5ee65"
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
    "toolchain/tests/artifact_size_policy_contract.cc",
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
    "toolchain/tests/toolchain_manifest_contract.cc",
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


def _digest_size(value, size):
    return {"sha256": _digest(value), "size": size}


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
        "input_count": len(INPUT_PATHS),
        "inputs": {
            path: _digest_size(f"input:{path}", index)
            for index, path in enumerate(INPUT_PATHS)
        },
        "object_comparisons": {
            name: _digest_size(f"object:{name}", index + 1)
            for index, name in enumerate(OBJECT_COMPARISON_NAMES)
        },
        "schema": "cupid.toolchain-contracts.v3",
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
            (path, 1, record["size"], record["sha256"])
            for path, record in sorted(manifest["inputs"].items())
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


def _author_request(
    *,
    manifest=None,
    observations=None,
    input_observations=None,
    bootstrap_observations=None,
    bootstrap_snapshot_sha256=None,
    seed_manifest_path=None,
    seed_manifest_bytes=None,
    seed_observations=None,
    object_observations=None,
):
    if manifest is None or observations is None:
        fixture_manifest, fixture_observations = _fixture()
        if manifest is None:
            manifest = fixture_manifest
        if observations is None:
            observations = fixture_observations
    if input_observations is None:
        input_observations = [
            (path, 1, record["size"], record["sha256"])
            for path, record in sorted(manifest["inputs"].items())
        ]
    if bootstrap_observations is None:
        bootstrap_observations = [
            (path, 1, record["size"], record["sha256"])
            for path, record in sorted(
                manifest["bootstrap"]["source_inputs"]["files"].items()
            )
        ]
    if bootstrap_snapshot_sha256 is None:
        bootstrap_snapshot_sha256 = manifest["bootstrap"]["source_inputs"][
            "sha256"
        ]
    if seed_manifest_path is None:
        seed_manifest_path = manifest["bootstrap"]["seed_manifest"]["path"]
    if seed_manifest_bytes is None or seed_observations is None:
        fixture_seed, fixture_seed_observations = _seed_fixture(manifest)
        if seed_manifest_bytes is None:
            seed_manifest_bytes = fixture_seed
        if seed_observations is None:
            seed_observations = fixture_seed_observations
    if object_observations is None:
        object_observations = [
            (name, 1, record["size"], record["sha256"])
            for name, record in sorted(
                manifest["object_comparisons"].items()
            )
        ]

    payload = bytearray(AUTHOR_MAGIC)
    for observation_set in (
        observations,
        input_observations,
        bootstrap_observations,
    ):
        payload.extend(struct.pack("<I", len(observation_set)))
        for name, kind, size, digest in observation_set:
            _append_bytes(payload, name.encode("ascii"))
            payload.extend(struct.pack("<IQ", kind, size))
            _append_bytes(payload, digest.encode("ascii"))
    _append_bytes(payload, bootstrap_snapshot_sha256.encode("ascii"))
    _append_bytes(payload, seed_manifest_path.encode("ascii"))
    _append_bytes(payload, seed_manifest_bytes)
    payload.extend(struct.pack("<I", len(seed_observations)))
    for name, kind, size, digest in seed_observations:
        _append_bytes(payload, name.encode("ascii"))
        payload.extend(struct.pack("<IQ", kind, size))
        _append_bytes(payload, digest.encode("ascii"))
    payload.extend(struct.pack("<I", len(object_observations)))
    for name, kind, size, digest in object_observations:
        _append_bytes(payload, name.encode("ascii"))
        payload.extend(struct.pack("<IQ", kind, size))
        _append_bytes(payload, digest.encode("ascii"))
    fixed_point = manifest["tool_fixed_point"]
    payload.extend(
        struct.pack(
            "<III",
            int(fixed_point["all_equal"]),
            fixed_point["c_objects"],
            len(fixed_point["compared_generations"]),
        )
    )
    for generation in fixed_point["compared_generations"]:
        _append_bytes(payload, generation.encode("ascii"))
    payload.extend(
        struct.pack(
            "<II",
            fixed_point["startup_objects"],
            fixed_point["tool_images"],
        )
    )
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
            '{"artifact_count":21,"artifact_total_bytes":652,'
            '"bootstrap_source_input_count":50,"input_count":70,'
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

    @unittest.skipIf(
        os.name == "nt" and shutil.which("wsl") is None,
        "WSL is required to execute the checked Linux stage-four seed",
    )
    def test_checked_stage_four_author_builds_and_emits_oracle_bytes(self):
        seed_manifest = (
            REPO_ROOT / "bootstrap/seeds/i386-linux/manifest.json"
        )
        with tempfile.TemporaryDirectory(
            prefix=".cupman3-checked-stage-four-", dir=REPO_ROOT
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
            manifest, observations = _fixture()
            request = workspace / "request.bin"
            request.write_bytes(
                _author_request(
                    manifest=manifest,
                    observations=observations,
                )
            )
            result = runner.run(executable, ("author", request), 120)
            cupidc_toolchain_contracts.require_live_seed_inputs(seed)

            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stderr, "")
            self.assertEqual(
                result.stdout.encode("ascii"),
                (json.dumps(manifest, indent=2, sort_keys=True) + "\n").encode(
                    "ascii"
                ),
            )

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
        object_observations = [
            (name, 1, record["size"], record["sha256"])
            for name, record in sorted(
                manifest["object_comparisons"].items()
            )
        ]

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
                object_observations=list(reversed(object_observations)),
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
            [
                (name, 1, record["size"], record["sha256"])
                for name, record in sorted(
                    manifest["object_comparisons"].items()
                )
            ],
            manifest["tool_fixed_point"],
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

    def test_author_object_reader_rejects_missing_duplicate_and_zero_size(self):
        manifest, observations = _fixture()
        objects = [
            (name, 1, record["size"], record["sha256"])
            for name, record in sorted(
                manifest["object_comparisons"].items()
            )
        ]
        duplicated = list(objects)
        duplicated[-1] = duplicated[0]
        zero_size = list(objects)
        name, kind, _size, digest = zero_size[0]
        zero_size[0] = (name, kind, 0, digest)
        cases = {
            "missing": objects[:-1],
            "duplicate": duplicated,
            "zero size": zero_size,
        }

        for name, changed in cases.items():
            with self.subTest(name=name):
                self.assert_author_failure(
                    _author_request(
                        manifest=manifest,
                        observations=observations,
                        object_observations=changed,
                    )
                )

    def test_author_output_binds_input_and_object_sizes(self):
        manifest, observations = _fixture()
        inputs = [
            (path, 1, record["size"] + 101, record["sha256"])
            for path, record in sorted(manifest["inputs"].items())
        ]
        objects = [
            (name, 1, record["size"] + 201, record["sha256"])
            for name, record in sorted(
                manifest["object_comparisons"].items()
            )
        ]

        baseline = self.run_author_request(
            _author_request(
                manifest=manifest,
                observations=observations,
                input_observations=inputs,
                object_observations=objects,
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
                object_observations=objects,
            )
        )
        self.assertEqual(changed_input.returncode, 0, changed_input.stderr)
        self.assertNotEqual(changed_input.stdout, baseline.stdout)

        changed_objects = list(objects)
        name, kind, size, digest = changed_objects[0]
        changed_objects[0] = (name, kind, size + 1, digest)
        changed_object = self.run_author_request(
            _author_request(
                manifest=manifest,
                observations=observations,
                input_observations=inputs,
                object_observations=changed_objects,
            )
        )
        self.assertEqual(changed_object.returncode, 0, changed_object.stderr)
        self.assertNotEqual(changed_object.stdout, baseline.stdout)

    def test_author_fixed_point_reader_rejects_wrong_generations_and_counts(self):
        cases = {
            "not equal": ("all_equal", False),
            "C object count": ("c_objects", 18),
            "generation count": (
                "compared_generations",
                ["stage-four"],
            ),
            "generation name": (
                "compared_generations",
                ["stage-two", "stage-four"],
            ),
            "startup count": ("startup_objects", 2),
            "tool count": ("tool_images", 4),
        }

        for name, (field, value) in cases.items():
            with self.subTest(name=name):
                manifest, observations = _fixture()
                manifest["tool_fixed_point"][field] = value
                self.assert_author_failure(
                    _author_request(
                        manifest=manifest,
                        observations=observations,
                    )
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

    def test_current_publication_inventory_counts_are_exact(self):
        self.assertEqual(len(INPUT_PATHS), 70)
        for input_count in (69, 71):
            with self.subTest(input_count=input_count):
                manifest, observations = _fixture()
                manifest["inputs"] = {
                    path: _digest_size(f"input:{path}", index)
                    for index, path in enumerate(INPUT_PATHS[:input_count])
                }
                if input_count > len(INPUT_PATHS):
                    manifest["inputs"]["toolchain/unexpected.h"] = (
                        _digest_size("unexpected", input_count)
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

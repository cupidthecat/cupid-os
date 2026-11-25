## @file
#  @brief Provide manifest protocol requests, build fixtures and expected inventories.
import hashlib
import json
import os
import shutil
import struct
import subprocess
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SOURCE = REPO_ROOT / "toolchain/tests/toolchain_manifest_contract.cc"
MAGIC = b"CUPMAN2\0"
AUTHOR_MAGIC = b"CUPMAN4\0"
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
    for name in (
        "cupidasm",
        "cupiddis",
        "cupidld",
        "cupidobj",
        "cupidc",
        "cupidbuild",
    )
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
BOOTSTRAP_OBJECT_NAMES = (
    "runtime",
    "ctool",
    "ctool_host",
    "elf32",
    "x86",
    "cupidasm",
    "cupidasm_main",
    "cupiddis",
    "cupiddis_main",
    "cupidobj",
    "cupidobj_main",
    "cupidld",
    "cupidld_main",
    "cupidc_pp",
    "cupidc_type",
    "cupidc_frontend",
    "cupidc_ir",
    "cupidc_emit",
    "cupidc_main",
    "cupidbuild",
    "cupidbuild_host",
    "cupidbuild_main",
    "seed_manifest",
    "seed_release",
    "contract_parse_internal",
    "cupidbuild_artifacts",
    "artifact_size_policy",
    "user_syscall_abi",
    "cupidbuild_user_abi",
    "start",
)
BOOTSTRAP_TOOL_NAMES = (
    "cupidasm",
    "cupiddis",
    "cupidld",
    "cupidobj",
    "cupidc",
    "cupidbuild",
)
BUILD_PLAN_SHA256 = (
    "48d6cc38b7a7362a83a911d2d3aaae8e79537c3f1744f3f5e7aac997728ed7f4"
)
SEED_MANIFEST_SHA256 = (
    "59c5c33672ee5839efd5a27cc1b90c090984fcf0be74d3fcb7418f68215aa2f7"
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
    "toolchain/artifact_size_policy.h",
    "toolchain/contract_parse_internal.cc",
    "toolchain/contract_parse_internal.h",
    "toolchain/ctool.h",
    "toolchain/ctool_host.h",
    "toolchain/cupidasm.h",
    "toolchain/cupidbuild.h",
    "toolchain/cupidbuild_artifacts.h",
    "toolchain/cupidbuild_host.h",
    "toolchain/cupidbuild_user_abi.h",
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
    "toolchain/hosted/i386-linux/include/stddef.h",
    "toolchain/hosted/i386-linux/include/stdint.h",
    "toolchain/hosted/i386-linux/include/stdio.h",
    "toolchain/hosted/i386-linux/include/stdlib.h",
    "toolchain/hosted/i386-linux/include/string.h",
    "toolchain/hosted/i386-linux/include/unistd.h",
    "toolchain/hosted/i386-linux/include/windows.h",
    "toolchain/hosted/i386-windows/cupidbuild_start.asm",
    "toolchain/hosted/i386-windows/final_path_start.asm",
    "toolchain/hosted/i386-windows/publication_runtime.cc",
    "toolchain/hosted/i386-windows/publication_start.asm",
    "toolchain/hosted/i386-windows/runtime.cc",
    "toolchain/hosted/i386-windows/start.asm",
    "toolchain/hosted/i386-windows/tool_start.asm",
    "toolchain/hosted/i386-windows/utf8_cupidbuild_start.asm",
    "toolchain/hosted/i386-windows/utf8_long_path_start.asm",
    "toolchain/hosted/i386-windows/utf8_publication_start.asm",
    "toolchain/hosted/i386-windows/utf8_tool_start.asm",
    "toolchain/hosted/i386-windows/windows_utf8.cc",
    "toolchain/native_utf8.h",
    "toolchain/path_encoding.cc",
    "toolchain/path_encoding.h",
    "toolchain/pe32.h",
    "toolchain/pe32_impl.h",
    "toolchain/seed_manifest.h",
    "toolchain/seed_release.h",
    "toolchain/tests/core_contract.cc",
    "toolchain/tests/cupidasm_contract.cc",
    "toolchain/tests/cupidasm_demos_contract.cc",
    "toolchain/tests/cupidasm_kernel_elf_contract.cc",
    "toolchain/tests/cupidc_exact_floating_literal_fixture.h",
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
    "toolchain/user_syscall_abi.h",
    "toolchain/x86.cc",
    "toolchain/x86.h",
    "tools/bootstrap_toolchain.py",
    "tools/cupidc_toolchain_contracts.py",
    "tools/user_syscall_abi.py",
    "user/cupid.h",
)
BOOTSTRAP_PATHS = (
    "link.ld",
    "toolchain/artifact_size_policy.cc",
    "toolchain/artifact_size_policy.h",
    "toolchain/contract_parse_internal.cc",
    "toolchain/contract_parse_internal.h",
    "toolchain/ctool.cc",
    "toolchain/ctool.h",
    "toolchain/ctool_host.cc",
    "toolchain/ctool_host.h",
    "toolchain/cupidasm.cc",
    "toolchain/cupidasm.h",
    "toolchain/cupidasm_main.cc",
    "toolchain/cupidbuild.cc",
    "toolchain/cupidbuild.h",
    "toolchain/cupidbuild_artifacts.cc",
    "toolchain/cupidbuild_artifacts.h",
    "toolchain/cupidbuild_host.cc",
    "toolchain/cupidbuild_host.h",
    "toolchain/cupidbuild_main.cc",
    "toolchain/cupidbuild_user_abi.cc",
    "toolchain/cupidbuild_user_abi.h",
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
    "toolchain/hosted/i386-linux/include/stddef.h",
    "toolchain/hosted/i386-linux/include/stdint.h",
    "toolchain/hosted/i386-linux/include/stdio.h",
    "toolchain/hosted/i386-linux/include/stdlib.h",
    "toolchain/hosted/i386-linux/include/string.h",
    "toolchain/hosted/i386-linux/include/unistd.h",
    "toolchain/hosted/i386-linux/include/windows.h",
    "toolchain/hosted/i386-linux/runtime.cc",
    "toolchain/hosted/i386-linux/start.asm",
    "toolchain/hosted/i386-windows/cupidbuild_start.asm",
    "toolchain/hosted/i386-windows/publication_runtime.cc",
    "toolchain/hosted/i386-windows/publication_start.asm",
    "toolchain/hosted/i386-windows/runtime.cc",
    "toolchain/hosted/i386-windows/start.asm",
    "toolchain/hosted/i386-windows/tool_start.asm",
    "toolchain/hosted/i386-windows/utf8_cupidbuild_start.asm",
    "toolchain/hosted/i386-windows/utf8_publication_start.asm",
    "toolchain/hosted/i386-windows/utf8_tool_start.asm",
    "toolchain/hosted/i386-windows/windows_utf8.cc",
    "toolchain/native_utf8.h",
    "toolchain/path_encoding.cc",
    "toolchain/path_encoding.h",
    "toolchain/pe32.h",
    "toolchain/pe32_impl.h",
    "toolchain/seed_manifest.cc",
    "toolchain/seed_manifest.h",
    "toolchain/seed_release.cc",
    "toolchain/seed_release.h",
    "toolchain/tests/hosted_i386_windows_contract.cc",
    "toolchain/tests/hosted_i386_windows_runtime_contract.cc",
    "toolchain/user_syscall_abi.cc",
    "toolchain/user_syscall_abi.h",
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
        str(SOURCE.parent.parent / "contract_parse_internal.cc"),
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
            "c_objects": 29,
            "compared_generations": ["stage-three", "stage-four"],
            "startup_objects": 1,
            "tool_images": 6,
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


def _profile_fixture(*, user_link_aliases=False, long_paths=False):
    manifest, observations = _fixture()
    inputs = manifest["bootstrap"]["source_inputs"]
    for enabled, path in (
        (user_link_aliases, "toolchain/hosted/i386-windows/final_path_start.asm"),
        (long_paths, "toolchain/hosted/i386-windows/utf8_long_path_start.asm"),
    ):
        if enabled:
            inputs["files"][path] = _digest_size("profile:" + path, 31)
    inputs["count"] = len(inputs["files"])
    inputs["sha256"] = _digest(_json_bytes(inputs["files"]))
    return manifest, observations


def _seed_fixture(manifest):
    seed_path = REPO_ROOT / "tests/fixtures/toolchain-manifest-classic-linux-seed.json"
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
    object_pairs=None,
    executable_pairs=None,
    bootstrap_object_pairs=None,
    bootstrap_tool_pairs=None,
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
    if object_pairs is None:
        object_pairs = _matching_object_pairs(manifest)
    if executable_pairs is None:
        executable_pairs = _matching_executable_pairs(manifest)
    if bootstrap_object_pairs is None:
        bootstrap_object_pairs = _matching_bootstrap_pairs(
            BOOTSTRAP_OBJECT_NAMES, "bootstrap-object"
        )
    if bootstrap_tool_pairs is None:
        bootstrap_tool_pairs = _matching_bootstrap_pairs(
            BOOTSTRAP_TOOL_NAMES, "bootstrap-tool"
        )

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
    for pairs in (
        object_pairs,
        executable_pairs,
        bootstrap_object_pairs,
        bootstrap_tool_pairs,
    ):
        payload.extend(struct.pack("<I", len(pairs)))
        for name, first_kind, first_bytes, second_kind, second_bytes in pairs:
            _append_bytes(payload, name.encode("ascii"))
            payload.extend(struct.pack("<I", first_kind))
            _append_bytes(payload, first_bytes)
            payload.extend(struct.pack("<I", second_kind))
            _append_bytes(payload, second_bytes)
    return bytes(payload)


def _captured_seed_fixture():
    """Frozen historical candidate metadata, not a producer proof."""
    payload = (REPO_ROOT / "tests/fixtures/toolchain-manifest-abi-candidate-linux-seed.json").read_bytes()
    document = json.loads(payload)
    observations = [(row["file"], 1, row["size"], row["sha256"])
                    for row in sorted(document["artifacts"], key=lambda row: row["file"])]
    return payload, observations


COMPLETE_ISO_OBJECTS = (
    "iso_fixture_bundle", "cupidbuild_iso", "cupidbuild_iso_capture",
    "cupidbuild_iso_image", "cupidbuild_iso_publication",
)


def _complete_context_fixture(*, long_paths=False, user_link_aliases=False):
    from tools import bootstrap_toolchain as seed
    payload, seed_observations = _captured_seed_fixture()
    document = json.loads(payload)
    document["build_plan"] = seed._candidate_build_plan(document["build_plan"])
    document["build_plan"]["sources"] = [row for row in document["build_plan"]["sources"]
        if row["name"] not in {"fat16_stage", "fat16_names", "disk_image",
                              *[name for name, _path, _gnu in seed.SOURCE_OWNER_SOURCES]}]
    document["build_plan"]["links"]["cupidbuild"] = list(seed.ISO_PUBLICATION_CUPIDBUILD_LINK)
    document["build_plan_sha256"] = seed._build_plan_sha256(document["build_plan"])
    document["provenance"]["source_input_count"] = 92
    seed_bytes = _json_bytes(document)
    manifest, observations = _profile_fixture(long_paths=long_paths, user_link_aliases=user_link_aliases)
    inputs = manifest["bootstrap"]["source_inputs"]
    for name in COMPLETE_ISO_OBJECTS:
        for suffix in (".cc", ".h"):
            path = "toolchain/" + name + suffix
            actual = (REPO_ROOT / path).read_bytes()
            inputs["files"][path] = {"size": len(actual), "sha256": _digest(actual)}
            if suffix == ".h":
                manifest["inputs"][path] = {"size": len(actual), "sha256": _digest(actual)}
    manifest["input_count"] = len(manifest["inputs"])
    inputs["count"] = len(inputs["files"])
    inputs["sha256"] = _digest(_json_bytes(inputs["files"]))
    manifest["bootstrap"]["build_plan_sha256"] = document["build_plan_sha256"]
    manifest["bootstrap"]["seed_manifest"]["sha256"] = _digest(seed_bytes)
    manifest["tool_fixed_point"]["c_objects"] = 34
    _matching_object_pairs(manifest)
    _matching_executable_pairs(manifest)
    return manifest, observations, seed_bytes, seed_observations


def _complete_context_request(mode, *, long_paths=False, user_link_aliases=False,
                              manifest=None, bootstrap_object_pairs=None, **kwargs):
    fixture, observations, seed_bytes, seed_observations = _complete_context_fixture(
        long_paths=long_paths, user_link_aliases=user_link_aliases)
    if manifest is None:
        manifest = fixture
    builder = _author_request if mode == "author" else _request
    if mode == "author":
        kwargs["bootstrap_object_pairs"] = (bootstrap_object_pairs if bootstrap_object_pairs is not None else
            _matching_bootstrap_pairs((*BOOTSTRAP_OBJECT_NAMES, *COMPLETE_ISO_OBJECTS), "bootstrap-object"))
    classic = builder(manifest=manifest, observations=observations, seed_manifest_bytes=seed_bytes,
                      seed_observations=seed_observations, **kwargs)
    return (b"CUPMAN5\0" if mode == "author" else b"CUPMAN6\0") + _seed_context_bytes(seed_bytes) + classic[8:]


DISK_FOUNDATION_OBJECTS = ("fat16_stage", "fat16_names", "disk_image")


def _disk_context_fixture(*, long_paths=False, user_link_aliases=False, release_inputs=True):
    from tools import bootstrap_toolchain as seed
    manifest, observations, seed_bytes, seed_observations = _complete_context_fixture(
        long_paths=long_paths, user_link_aliases=user_link_aliases)
    document = json.loads(seed_bytes)
    document["build_plan"] = seed._candidate_build_plan(document["build_plan"])
    document["build_plan"]["sources"] = [row for row in document["build_plan"]["sources"]
        if row["name"] not in {name for name, _path, _gnu in seed.SOURCE_OWNER_SOURCES}]
    document["build_plan"]["links"]["cupidbuild"] = list(seed.PROMOTED_CUPIDBUILD_LINK)
    document["build_plan_sha256"] = seed._build_plan_sha256(document["build_plan"])
    document["provenance"]["source_input_count"] = 99
    seed_bytes = _json_bytes(document)
    inputs = manifest["bootstrap"]["source_inputs"]
    paths = ["toolchain/" + name + suffix for name in DISK_FOUNDATION_OBJECTS for suffix in (".cc", ".h")]
    paths.append("toolchain/fat16_name_profiles.inc")
    for path in paths:
        payload = (REPO_ROOT / path).read_bytes()
        row = {"size": len(payload), "sha256": _digest(payload)}
        inputs["files"][path] = row
        if path.endswith(".h"):
            manifest["inputs"][path] = row.copy()
    if release_inputs:
        for path in ("tools/__init__.py", "tools/bootstrap_user_abi.py",
                     "tools/bootstrap_stage_release.py", "tools/seed_release_identity.py"):
            payload = (REPO_ROOT / path).read_bytes()
            manifest["inputs"][path] = {"size": len(payload), "sha256": _digest(payload)}
    manifest["input_count"] = len(manifest["inputs"])
    inputs["count"] = len(inputs["files"])
    inputs["sha256"] = _digest(_json_bytes(inputs["files"]))
    manifest["bootstrap"]["build_plan_sha256"] = document["build_plan_sha256"]
    manifest["bootstrap"]["seed_manifest"]["sha256"] = _digest(seed_bytes)
    manifest["tool_fixed_point"]["c_objects"] = 37
    return manifest, observations, seed_bytes, seed_observations


def _disk_context_request(mode, *, long_paths=False, user_link_aliases=False,
                          release_inputs=True, manifest=None, bootstrap_object_pairs=None,
                          captured=True, **kwargs):
    fixture, observations, seed_bytes, seed_observations = _disk_context_fixture(
        long_paths=long_paths, user_link_aliases=user_link_aliases, release_inputs=release_inputs)
    builder = _author_request if mode == "author" else _request
    if mode == "author":
        kwargs["bootstrap_object_pairs"] = (bootstrap_object_pairs if bootstrap_object_pairs is not None else
            _matching_bootstrap_pairs((*BOOTSTRAP_OBJECT_NAMES, *COMPLETE_ISO_OBJECTS,
                                       *DISK_FOUNDATION_OBJECTS), "bootstrap-object"))
    classic = builder(manifest=fixture if manifest is None else manifest,
        observations=observations, seed_manifest_bytes=seed_bytes,
        seed_observations=seed_observations, **kwargs)
    if not captured:
        return classic
    return (b"CUPMAN5\0" if mode == "author" else b"CUPMAN6\0") + _seed_context_bytes(seed_bytes) + classic[8:]


OWNER_OBJECTS = ('cupidbuild_source_path', 'cupidbuild_source_file', 'cupidbuild_source_stage',
                 'cupidbuild_disk_integer', 'cupidbuild_disk_options', 'cupidbuild_disk_input_discovery',
                 'path_encoding')
OWNER_HEADERS = tuple('toolchain/' + name + '.h' for name in OWNER_OBJECTS[:-1])
OWNER_SHARED_INPUTS = ('toolchain/hosted/i386-linux/include/cupid_runtime.h',
                       'toolchain/hosted/i386-windows/observer_thread.asm')
OWNER_BOOTSTRAP_INPUTS = (*OWNER_HEADERS, *OWNER_SHARED_INPUTS,
    *('toolchain/' + name + '.cc' for name in OWNER_OBJECTS[:-1]),
    'toolchain/cupidbuild_disk_integer_tables.inc')


def _owner_context_fixture(*, source_spelling=False, **profile):
    manifest, observations, seed_bytes, seed_observations = _disk_context_fixture(**profile)
    inputs = manifest['bootstrap']['source_inputs']
    for path in OWNER_BOOTSTRAP_INPUTS:
        data = (REPO_ROOT / path).read_bytes()
        row = {'size': len(data), 'sha256': _digest(data)}
        inputs['files'][path] = row
        if path in (*OWNER_HEADERS, *OWNER_SHARED_INPUTS):
            manifest['inputs'][path] = row.copy()
    if source_spelling:
        if not profile.get('release_inputs', True):
            raise ValueError('the source-spelling fixture requires release inputs')
        path = 'toolchain/tests/cupidc_source_spelling_fixture.h'
        data = (REPO_ROOT / path).read_bytes()
        manifest['inputs'][path] = {'size': len(data), 'sha256': _digest(data)}
    manifest['input_count'] = len(manifest['inputs'])
    inputs['count'] = len(inputs['files'])
    inputs['sha256'] = _digest(_json_bytes(inputs['files']))
    manifest['bootstrap']['build_plan_sha256'] = 'e58a047f92d21a9ffbae6cc19726477f95ca3a872f1238d9cbab58dae950faaa'
    manifest['tool_fixed_point']['c_objects'] = 44
    return manifest, observations, seed_bytes, seed_observations


def _owner_context_request(mode, *, manifest=None, bootstrap_object_pairs=None,
                           input_observations=None, **profile):
    fixture, observations, seed_bytes, seed_observations = _owner_context_fixture(**profile)
    kwargs = {}
    if input_observations is not None:
        kwargs['input_observations'] = input_observations
    if mode == 'author':
        kwargs['bootstrap_object_pairs'] = bootstrap_object_pairs if bootstrap_object_pairs is not None else (
            _matching_bootstrap_pairs((*BOOTSTRAP_OBJECT_NAMES, *COMPLETE_ISO_OBJECTS,
                                      *DISK_FOUNDATION_OBJECTS, *OWNER_OBJECTS), 'bootstrap-object'))
    builder = _author_request if mode == 'author' else _request
    payload = builder(manifest=fixture if manifest is None else manifest,
        observations=observations, seed_manifest_bytes=seed_bytes, seed_observations=seed_observations, **kwargs)
    return (b'CUPMAN5\0' if mode == 'author' else b'CUPMAN6\0') + _seed_context_bytes(seed_bytes) + payload[8:]


def _seed_context_bytes(seed_bytes, *, manifest_digest=None, plan_digest=None, facts=None):
    document = json.loads(seed_bytes)
    result = bytearray()
    _append_bytes(result, (manifest_digest or _digest(seed_bytes)).encode("ascii"))
    _append_bytes(result, (plan_digest or document["build_plan_sha256"]).encode("ascii"))
    if facts is None:
        facts = [(row["file"], row["size"], row["sha256"])
                 for row in sorted(document["artifacts"], key=lambda row: row["file"])]
    result.extend(struct.pack("<I", len(facts)))
    for name, size, digest in facts:
        _append_bytes(result, name.encode("ascii"))
        result.extend(struct.pack("<Q", size))
        _append_bytes(result, digest.encode("ascii"))
    return bytes(result)


def _context_request(mode, *, seed_bytes=None, observations=None, context=None,
                     manifest=None, artifact_observations=None):
    if seed_bytes is None or observations is None:
        captured_bytes, captured_observations = _captured_seed_fixture()
        seed_bytes = captured_bytes if seed_bytes is None else seed_bytes
        observations = captured_observations if observations is None else observations
    if manifest is None or artifact_observations is None:
        fixture_manifest, fixture_artifacts = _fixture()
        manifest = fixture_manifest if manifest is None else manifest
        artifact_observations = fixture_artifacts if artifact_observations is None else artifact_observations
    manifest["bootstrap"]["seed_manifest"]["sha256"] = _digest(seed_bytes)
    builder = _author_request if mode == "author" else _request
    classic = builder(manifest=manifest, observations=artifact_observations,
                      seed_manifest_bytes=seed_bytes, seed_observations=observations)
    return (b"CUPMAN5\0" if mode == "author" else b"CUPMAN6\0") + (
        _seed_context_bytes(seed_bytes) if context is None else context) + classic[8:]


def _matching_object_pairs(manifest):
    object_pairs = []
    for index, name in enumerate(OBJECT_COMPARISON_NAMES):
        object_bytes = bytes([index + 1]) * (index + 1)
        manifest["object_comparisons"][name] = {
            "sha256": _digest(object_bytes),
            "size": len(object_bytes),
        }
        object_pairs.append((name, 1, object_bytes, 1, object_bytes))
    return object_pairs


def _matching_executable_pairs(manifest):
    pairs = []
    for name in CONTRACT_NAMES:
        executable_bytes = f"checked:{name}-contract.elf\n".encode("ascii")
        manifest["comparisons"][name] = _digest(executable_bytes)
        pairs.append(
            (name, 1, executable_bytes, 1, executable_bytes)
        )
    runtime_bytes = b"checked:cupidc-runtime-contract.elf\n"
    manifest["comparisons"]["runtime"] = _digest(runtime_bytes)
    pairs.append(("runtime", 1, runtime_bytes, 1, runtime_bytes))
    return pairs


def _matching_bootstrap_pairs(names, prefix):
    return [
        (
            name,
            1,
            f"{prefix}:{name}\n".encode("ascii"),
            1,
            f"{prefix}:{name}\n".encode("ascii"),
        )
        for name in names
    ]

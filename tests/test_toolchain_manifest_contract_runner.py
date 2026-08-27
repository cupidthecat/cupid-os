import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tests.test_toolchain_manifest_contract import (
    _fixture,
    _seed_fixture,
)
from tools import bootstrap_toolchain, toolchain_manifest_contract


REPO_ROOT = Path(__file__).resolve().parents[1]
REAL_CAPTURE_LIVE_CLOSURE = (
    toolchain_manifest_contract._capture_live_manifest_closure
)
REAL_REQUIRE_LIVE_MEMBERSHIP = (
    toolchain_manifest_contract._require_live_closure_membership
)
REAL_VERIFY_PUBLICATION_INPUTS = (
    toolchain_manifest_contract.cupidc_toolchain_contracts
    .verify_publication_inputs
)

EXPECTED_BUILD_INPUTS = (
    "toolchain/Makefile",
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
    "toolchain/hosted/i386-windows/runtime.cc",
    "toolchain/hosted/i386-windows/tool_start.asm",
    "toolchain/tests/artifact_size_policy_contract.cc",
    "toolchain/tests/toolchain_manifest_contract.cc",
    "tools/artifact_size_policy.py",
    "tools/bootstrap_toolchain.py",
    "tools/cupidc_toolchain_contracts.py",
    "tools/toolchain_manifest_contract.py",
)


def _write_publication_at(output: Path):
    output.mkdir(parents=True)
    manifest, observations = _fixture()
    for name, _kind, _size, _digest in observations:
        (output / name).write_bytes(f"checked:{name}\n".encode("ascii"))
    (output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="ascii",
    )
    return output, manifest, observations


def _write_publication(root: Path):
    return _write_publication_at(root / "toolchain/build/cupidc-contracts")


def _rewrite_with_live_closure(output: Path, manifest):
    root = REPO_ROOT.resolve()
    contract_inputs = (
        toolchain_manifest_contract.cupidc_toolchain_contracts
        ._contract_input_paths(root)
    )
    manifest["inputs"] = (
        toolchain_manifest_contract.cupidc_toolchain_contracts
        ._snapshot_contract_inputs(root, contract_inputs)
    )
    manifest["input_count"] = len(manifest["inputs"])
    seed_path = root / "bootstrap/seeds/i386-linux/manifest.json"
    seed = bootstrap_toolchain.verify_seed_inputs(seed_path)
    manifest["bootstrap"]["seed_manifest"] = {
        "path": seed_path.relative_to(root).as_posix(),
        "sha256": seed.manifest_sha256,
    }
    manifest["bootstrap"]["build_plan_sha256"] = seed.manifest[
        "build_plan_sha256"
    ]
    source_files = (
        toolchain_manifest_contract.cupidc_toolchain_contracts
        .capture_source_snapshot(root, seed.manifest["build_plan"])
    )
    manifest["bootstrap"]["source_inputs"] = {
        "count": len(source_files),
        "files": source_files,
        "sha256": (
            toolchain_manifest_contract.cupidc_toolchain_contracts
            ._snapshot_sha256(source_files)
        ),
    }
    (output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n",
        encoding="ascii",
    )


def _expected_report():
    return {
        "artifact_count": 22,
        "artifact_total_bytes": 682,
        "bootstrap_source_input_count": 58,
        "input_count": 75,
        "schema": "cupid.toolchain-manifest-verification.v1",
    }


def _decode_request(request: bytes):
    if request[:8] != b"CUPMAN2\0":
        raise AssertionError("request magic differs")
    offset = 8

    def take_u32():
        nonlocal offset
        value = struct.unpack_from("<I", request, offset)[0]
        offset += 4
        return value

    def take_u64():
        nonlocal offset
        value = struct.unpack_from("<Q", request, offset)[0]
        offset += 8
        return value

    def take_bytes():
        nonlocal offset
        size = take_u32()
        value = request[offset : offset + size]
        offset += size
        return value

    manifest = take_bytes()
    def take_observations():
        observations = []
        for _ in range(take_u32()):
            observations.append(
                (
                    take_bytes().decode("ascii"),
                    take_u32(),
                    take_u64(),
                    take_bytes().decode("ascii"),
                )
            )
        return observations

    artifact_observations = take_observations()
    input_observations = take_observations()
    bootstrap_observations = take_observations()
    seed_path = take_bytes().decode("ascii")
    seed_bytes = take_bytes()
    seed_observations = take_observations()
    if offset != len(request):
        raise AssertionError("request has trailing bytes")
    return {
        "artifact_observations": artifact_observations,
        "bootstrap_observations": bootstrap_observations,
        "input_observations": input_observations,
        "manifest": manifest,
        "seed_bytes": seed_bytes,
        "seed_observations": seed_observations,
        "seed_path": seed_path,
    }


def _fixture_closure(_reader, _root, report):
    input_observations = tuple(
        (path, 1, record["size"], record["sha256"])
        for path, record in sorted(report["inputs"].items())
    )
    source_files = report["bootstrap"]["source_inputs"]["files"]
    bootstrap_observations = tuple(
        (path, 1, record["size"], record["sha256"])
        for path, record in sorted(source_files.items())
    )
    seed_bytes, seed_observations = _seed_fixture(report)
    return (
        input_observations,
        bootstrap_observations,
        report["bootstrap"]["seed_manifest"]["path"],
        seed_bytes,
        tuple(seed_observations),
        (),
    )


class ToolchainManifestContractRunnerTests(unittest.TestCase):
    def setUp(self):
        self.verify_inputs_patch = mock.patch.object(
            toolchain_manifest_contract.cupidc_toolchain_contracts,
            "verify_publication_inputs",
        )
        self.verify_inputs = self.verify_inputs_patch.start()
        self.addCleanup(self.verify_inputs_patch.stop)
        self.capture_closure_patch = mock.patch.object(
            toolchain_manifest_contract,
            "_capture_live_manifest_closure",
            side_effect=_fixture_closure,
        )
        self.capture_closure = self.capture_closure_patch.start()
        self.addCleanup(self.capture_closure_patch.stop)
        self.membership_patch = mock.patch.object(
            toolchain_manifest_contract,
            "_require_live_closure_membership",
        )
        self.membership = self.membership_patch.start()
        self.addCleanup(self.membership_patch.stop)

    def test_linked_publication_is_rejected_before_directory_traversal(self):
        reader = mock.Mock()
        reader.directory_snapshot.side_effect = (
            toolchain_manifest_contract.artifact_size_policy.SizePolicyError(
                "path component is linked or reparse"
            )
        )
        with self.assertRaisesRegex(
            toolchain_manifest_contract.ToolchainManifestContractError,
            "cannot inspect Toolchain publication",
        ):
            toolchain_manifest_contract._capture_request(
                reader,
                "toolchain/build/cupidc-contracts",
            )
        reader.capture.assert_not_called()

    def test_contract_build_input_closure_is_exact(self):
        self.assertEqual(
            toolchain_manifest_contract.BUILD_INPUTS,
            EXPECTED_BUILD_INPUTS,
        )

    def test_make_shaped_paths_are_resolved_from_the_repository_root(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            output_logical, output = (
                toolchain_manifest_contract._resolve_repository_path(
                    root,
                    Path("toolchain/build/cupidc-contracts"),
                    "Toolchain publication",
                )
            )
            seed_logical, seed = (
                toolchain_manifest_contract._resolve_repository_path(
                    root,
                    Path("bootstrap/seeds/i386-windows/manifest.json"),
                    "execution seed manifest",
                )
            )
        self.assertEqual(
            output_logical, "toolchain/build/cupidc-contracts"
        )
        self.assertEqual(
            output, root / "toolchain/build/cupidc-contracts"
        )
        self.assertEqual(
            seed_logical, "bootstrap/seeds/i386-windows/manifest.json"
        )
        self.assertEqual(
            seed, root / "bootstrap/seeds/i386-windows/manifest.json"
        )

    def test_make_shaped_cli_ignores_a_conflicting_tools_package(self):
        with tempfile.TemporaryDirectory() as directory:
            shadow_root = Path(directory)
            shadow_tools = shadow_root / "tools"
            shadow_tools.mkdir()
            (shadow_tools / "__init__.py").write_text(
                'raise RuntimeError("shadow tools package imported")\n',
                encoding="ascii",
            )
            environment = os.environ.copy()
            environment["PYTHONPATH"] = str(shadow_root)
            result = subprocess.run(
                [
                    sys.executable,
                    str(REPO_ROOT / "tools/toolchain_manifest_contract.py"),
                    "--help",
                ],
                cwd=REPO_ROOT / "toolchain",
                env=environment,
                capture_output=True,
                text=True,
                check=False,
            )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("verify", result.stdout)
        self.assertNotIn("shadow tools package imported", result.stderr)
        self.assertNotIn("Traceback", result.stderr)

    def test_same_metadata_trust_input_rewrite_is_rejected(self):
        reader = mock.Mock()
        capture = mock.Mock()
        capture.payload = b"changed"
        reader.capture.return_value = (capture, None)
        with self.assertRaisesRegex(
            toolchain_manifest_contract.ToolchainManifestContractError,
            "trust input changed",
        ):
            toolchain_manifest_contract._require_payloads_unchanged(
                reader,
                (("tools/toolchain_manifest_contract.py", b"original"),),
                "contract trust input",
            )

    def test_execution_seed_rejects_an_unlisted_tool_image(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            seed = root / "bootstrap/seeds/i386-windows"
            seed.mkdir(parents=True)
            files = tuple(
                f"{name}.exe"
                for name in (
                    "cupidasm",
                    "cupidc",
                    "cupiddis",
                    "cupidld",
                    "cupidobj",
                    "cupidbuild",
                )
            )
            for name in (*files, "rogue.exe"):
                (seed / name).write_bytes(b"image")
            with self.assertRaisesRegex(
                toolchain_manifest_contract.ToolchainManifestContractError,
                "artifact membership differs",
            ):
                with toolchain_manifest_contract.artifact_size_policy._PinnedRepository(
                    root
                ) as reader:
                    toolchain_manifest_contract._seed_directory_membership(
                        reader,
                        "bootstrap/seeds/i386-windows/manifest.json",
                        "exe",
                        files,
                    )

    def test_execution_seed_parent_link_is_rejected_by_the_pinned_reader(self):
        with tempfile.TemporaryDirectory() as root_directory:
            with tempfile.TemporaryDirectory() as outside_directory:
                root = Path(root_directory)
                outside = Path(outside_directory)
                source = outside / "seed"
                source.mkdir()
                (source / "manifest.json").write_text(
                    "{}\n", encoding="ascii"
                )
                alias = root / "seed-link"
                try:
                    alias.symlink_to(source, target_is_directory=True)
                except OSError as error:
                    self.skipTest(
                        f"directory links are unavailable: {error}"
                    )
                with toolchain_manifest_contract.artifact_size_policy._PinnedRepository(
                    root
                ) as reader:
                    with self.assertRaises(
                        toolchain_manifest_contract.artifact_size_policy.SizePolicyError
                    ):
                        toolchain_manifest_contract.artifact_size_policy._required_capture(
                            reader,
                            "seed-link/manifest.json",
                            "execution seed manifest",
                        )

    def test_final_membership_check_rejects_a_new_contract_input(self):
        seed_manifest = bootstrap_toolchain.verify_seed_inputs(
            REPO_ROOT / "bootstrap/seeds/i386-linux/manifest.json"
        ).manifest
        seed_bytes = (
            REPO_ROOT / "bootstrap/seeds/i386-linux/manifest.json"
        ).read_bytes()
        bootstrap_paths = tuple(
            bootstrap_toolchain._source_input_paths(
                REPO_ROOT, seed_manifest["build_plan"]
            )
        )
        input_observations = (("toolchain/ctool.h", 1, 1, "0" * 64),)
        bootstrap_observations = tuple(
            (path, 1, 1, "0" * 64) for path in bootstrap_paths
        )
        with mock.patch.object(
            toolchain_manifest_contract,
            "_contract_input_logical_paths",
            return_value=(
                "toolchain/ctool.h",
                "toolchain/new-contract-input.h",
            ),
        ):
            with mock.patch.object(
                toolchain_manifest_contract,
                "_bootstrap_input_logical_paths",
                return_value=bootstrap_paths,
            ):
                with self.assertRaisesRegex(
                    toolchain_manifest_contract.ToolchainManifestContractError,
                    "input membership changed",
                ):
                    REAL_REQUIRE_LIVE_MEMBERSHIP(
                        mock.sentinel.reader,
                        input_observations,
                        bootstrap_observations,
                        seed_bytes,
                    )

    def test_verify_binds_one_pinned_manifest_and_artifact_snapshot(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output, manifest, observations = _write_publication(root)
            execution_manifest = root / "execution.json"
            execution_manifest.write_text("{}\n", encoding="ascii")
            captured = {}

            def run_contract(
                _reader, _root, selected_manifest, request, timeout
            ):
                captured["execution_manifest"] = selected_manifest
                captured["request"] = request
                captured["timeout"] = timeout
                return _expected_report()

            with mock.patch.object(
                toolchain_manifest_contract,
                "_build_and_run_contract",
                side_effect=run_contract,
            ):
                report = toolchain_manifest_contract.verify_with_contract(
                    root,
                    output,
                    execution_manifest,
                    timeout=37,
                )

            decoded = _decode_request(captured["request"])
            self.assertEqual(
                decoded["manifest"], (output / "manifest.json").read_bytes()
            )
            self.assertEqual(
                decoded["artifact_observations"],
                sorted(observations),
            )
            self.assertEqual(len(decoded["input_observations"]), 75)
            self.assertIn(
                "toolchain/x86.cc",
                {
                    path
                    for path, _kind, _size, _digest in decoded[
                        "input_observations"
                    ]
                },
            )
            self.assertEqual(len(decoded["bootstrap_observations"]), 58)
            self.assertEqual(len(decoded["seed_observations"]), 6)
            self.assertEqual(
                decoded["seed_path"],
                manifest["bootstrap"]["seed_manifest"]["path"],
            )
            self.assertEqual(captured["execution_manifest"], "execution.json")
            self.assertEqual(captured["timeout"], 37)
            self.assertEqual(report["artifact_count"], len(manifest["artifacts"]))

    def test_verify_checks_live_publication_inputs_before_and_after_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output, _manifest, _observations = _write_publication(root)
            execution_manifest = root / "execution.json"
            execution_manifest.write_text("{}\n", encoding="ascii")
            with mock.patch.object(
                toolchain_manifest_contract,
                "_build_and_run_contract",
                return_value=_expected_report(),
            ):
                toolchain_manifest_contract.verify_with_contract(
                    root,
                    output,
                    execution_manifest,
                )
            self.assertEqual(self.verify_inputs.call_count, 2)
            for call in self.verify_inputs.call_args_list:
                self.assertEqual(call.args[0], root.resolve())
                self.assertEqual(
                    call.args[1]["schema"],
                    "cupid.toolchain-contracts.v3",
                )

    def test_second_live_input_check_rejects_drift_after_the_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output, _manifest, _observations = _write_publication(root)
            execution_manifest = root / "execution.json"
            execution_manifest.write_text("{}\n", encoding="ascii")
            self.verify_inputs.side_effect = (
                None,
                toolchain_manifest_contract.cupidc_toolchain_contracts.ContractError(
                    "published contract inputs differ from the live source"
                ),
            )
            with mock.patch.object(
                toolchain_manifest_contract,
                "_build_and_run_contract",
                return_value=_expected_report(),
            ):
                with self.assertRaisesRegex(
                    toolchain_manifest_contract.ToolchainManifestContractError,
                    "published contract inputs differ",
                ):
                    toolchain_manifest_contract.verify_with_contract(
                        root, output, execution_manifest
                    )
        self.assertEqual(self.verify_inputs.call_count, 2)

    def test_python_oracle_uses_the_same_captured_manifest_as_the_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output, manifest_a, _observations = _write_publication(root)
            execution_manifest = root / "execution.json"
            execution_manifest.write_text("{}\n", encoding="ascii")
            manifest_b = json.loads(json.dumps(manifest_a))
            first_input = sorted(manifest_a["inputs"])[0]
            manifest_a["inputs"][first_input]["sha256"] = "0" * 64
            manifest_a_bytes = (
                json.dumps(manifest_a, indent=2, sort_keys=True) + "\n"
            ).encode("ascii")
            manifest_b_bytes = (
                json.dumps(manifest_b, indent=2, sort_keys=True) + "\n"
            ).encode("ascii")
            manifest_path = output / "manifest.json"
            manifest_path.write_bytes(manifest_a_bytes)
            manifest_status = manifest_path.stat()
            verify_publication = (
                toolchain_manifest_contract.cupidc_toolchain_contracts
                .verify_publication
            )

            def swap_during_oracle(path):
                manifest_path.write_bytes(manifest_b_bytes)
                try:
                    return verify_publication(path)
                finally:
                    manifest_path.write_bytes(manifest_a_bytes)
                    os.utime(
                        manifest_path,
                        ns=(
                            manifest_status.st_atime_ns,
                            manifest_status.st_mtime_ns,
                        ),
                    )

            def reject_manifest_a(_root, report):
                if report["inputs"][first_input]["sha256"] == "0" * 64:
                    raise toolchain_manifest_contract.cupidc_toolchain_contracts.ContractError(
                        "published contract inputs differ from the live source"
                    )

            self.verify_inputs.side_effect = reject_manifest_a
            with mock.patch.object(
                toolchain_manifest_contract,
                "_build_and_run_contract",
                return_value=_expected_report(),
            ):
                with mock.patch.object(
                    toolchain_manifest_contract.cupidc_toolchain_contracts,
                    "verify_publication",
                    side_effect=swap_during_oracle,
                ):
                    with self.assertRaisesRegex(
                        toolchain_manifest_contract.ToolchainManifestContractError,
                        "published contract inputs differ",
                    ):
                        toolchain_manifest_contract.verify_with_contract(
                            root,
                            output,
                            execution_manifest,
                        )

    def test_checked_seed_build_runs_the_manifest_contract(self):
        execution_manifest = REPO_ROOT / (
            "bootstrap/seeds/i386-windows/manifest.json"
            if os.name == "nt"
            else "bootstrap/seeds/i386-linux/manifest.json"
        )
        if not execution_manifest.is_file():
            self.skipTest("the checked execution seed is unavailable")
        with tempfile.TemporaryDirectory(
            prefix=".toolchain-manifest-publication-", dir=REPO_ROOT
        ) as directory:
            output, _manifest, _observations = _write_publication_at(
                Path(directory) / "publication"
            )
            _rewrite_with_live_closure(output, _manifest)
            with mock.patch.object(
                toolchain_manifest_contract,
                "_capture_live_manifest_closure",
                side_effect=REAL_CAPTURE_LIVE_CLOSURE,
            ):
                with mock.patch.object(
                    toolchain_manifest_contract,
                    "_require_live_closure_membership",
                    side_effect=REAL_REQUIRE_LIVE_MEMBERSHIP,
                ):
                    with mock.patch.object(
                        toolchain_manifest_contract.cupidc_toolchain_contracts,
                        "verify_publication_inputs",
                        side_effect=REAL_VERIFY_PUBLICATION_INPUTS,
                    ):
                        report = (
                            toolchain_manifest_contract.verify_with_contract(
                                REPO_ROOT,
                                output,
                                execution_manifest,
                                timeout=120,
                            )
                        )
        self.assertEqual(
            report,
            _expected_report(),
        )

    def test_verify_rejects_a_report_that_differs_from_the_oracle(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output, _manifest, _observations = _write_publication(root)
            execution_manifest = root / "execution.json"
            execution_manifest.write_text("{}\n", encoding="ascii")
            wrong = _expected_report()
            wrong["artifact_count"] = 20
            with mock.patch.object(
                toolchain_manifest_contract,
                "_build_and_run_contract",
                return_value=wrong,
            ):
                with self.assertRaisesRegex(
                    toolchain_manifest_contract.ToolchainManifestContractError,
                    "differs from the independent Python oracle",
                ):
                    toolchain_manifest_contract.verify_with_contract(
                        root, output, execution_manifest
                    )

    def test_verify_rejects_live_artifact_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output, _manifest, observations = _write_publication(root)
            execution_manifest = root / "execution.json"
            execution_manifest.write_text("{}\n", encoding="ascii")

            def change_artifact(
                _reader, _root, _manifest, _request, _timeout
            ):
                (output / observations[0][0]).write_bytes(b"changed\n")
                return _expected_report()

            with mock.patch.object(
                toolchain_manifest_contract,
                "_build_and_run_contract",
                side_effect=change_artifact,
            ):
                with self.assertRaisesRegex(
                    toolchain_manifest_contract.ToolchainManifestContractError,
                    "publication changed while the contract ran",
                ):
                    toolchain_manifest_contract.verify_with_contract(
                        root, output, execution_manifest
                    )

    def test_verify_rejects_same_metadata_artifact_content_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output, _manifest, observations = _write_publication(root)
            execution_manifest = root / "execution.json"
            execution_manifest.write_text("{}\n", encoding="ascii")

            def change_artifact(
                _reader, _root, _manifest, _request, _timeout
            ):
                target = output / observations[0][0]
                status = target.stat()
                original = target.read_bytes()
                target.write_bytes(bytes([original[0] ^ 1]) + original[1:])
                os.utime(
                    target,
                    ns=(status.st_atime_ns, status.st_mtime_ns),
                )
                return _expected_report()

            with mock.patch.object(
                toolchain_manifest_contract,
                "_build_and_run_contract",
                side_effect=change_artifact,
            ):
                with self.assertRaisesRegex(
                    toolchain_manifest_contract.ToolchainManifestContractError,
                    "publication changed while the contract ran",
                ):
                    toolchain_manifest_contract.verify_with_contract(
                        root, output, execution_manifest
                    )

    def test_verify_rejects_publication_membership_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output, _manifest, _observations = _write_publication(root)
            execution_manifest = root / "execution.json"
            execution_manifest.write_text("{}\n", encoding="ascii")

            def add_artifact(_reader, _root, _manifest, _request, _timeout):
                (output / "unexpected.elf").write_bytes(b"unexpected\n")
                return _expected_report()

            with mock.patch.object(
                toolchain_manifest_contract,
                "_build_and_run_contract",
                side_effect=add_artifact,
            ):
                with self.assertRaisesRegex(
                    toolchain_manifest_contract.ToolchainManifestContractError,
                    "publication changed while the contract ran",
                ):
                    toolchain_manifest_contract.verify_with_contract(
                        root, output, execution_manifest
                    )

    @unittest.skipIf(os.name == "nt", "POSIX rename semantics are required")
    def test_verify_rejects_repository_root_replaced_by_a_link(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            root = parent / "root"
            root.mkdir()
            output, _manifest, _observations = _write_publication(root)
            execution_manifest = root / "execution.json"
            execution_manifest.write_text("{}\n", encoding="ascii")
            moved = parent / "moved-root"

            def replace_root(
                _reader, _root, _manifest, _request, _timeout
            ):
                root.rename(moved)
                root.symlink_to(moved, target_is_directory=True)
                return _expected_report()

            try:
                with mock.patch.object(
                    toolchain_manifest_contract,
                    "_build_and_run_contract",
                    side_effect=replace_root,
                ):
                    with self.assertRaisesRegex(
                        toolchain_manifest_contract.ToolchainManifestContractError,
                        "repository root changed",
                    ):
                        toolchain_manifest_contract.verify_with_contract(
                            root, output, execution_manifest
                        )
            finally:
                if root.is_symlink():
                    root.unlink()
                if moved.exists():
                    moved.rename(root)

    @unittest.skipIf(os.name == "nt", "POSIX rename semantics are required")
    def test_one_pinned_reader_survives_a_repository_swap_back(self):
        with tempfile.TemporaryDirectory() as directory:
            parent = Path(directory)
            root = parent / "root"
            root.mkdir()
            output, _manifest, _observations = _write_publication(root)
            execution_manifest = root / "execution.json"
            execution_manifest.write_text("{}\n", encoding="ascii")
            original_manifest = (output / "manifest.json").read_bytes()
            original_names = tuple(sorted(path.name for path in output.iterdir()))
            moved = parent / "moved-root"

            def swap_root_back(
                reader, _root, _manifest, _request, _timeout
            ):
                root.rename(moved)
                replacement = root / "toolchain/build/cupidc-contracts"
                replacement.mkdir(parents=True)
                replacement_manifest = replacement / "manifest.json"
                replacement_manifest.write_bytes(b"replacement\n")
                try:
                    captured = (
                        toolchain_manifest_contract.artifact_size_policy
                        ._required_capture(
                            reader,
                            "toolchain/build/cupidc-contracts/manifest.json",
                            "Toolchain publication manifest",
                        )
                    )
                    _status, names = reader.directory_snapshot(
                        "toolchain/build/cupidc-contracts"
                    )
                    self.assertEqual(captured, original_manifest)
                    self.assertEqual(names, original_names)
                finally:
                    replacement_manifest.unlink()
                    replacement.rmdir()
                    replacement.parent.rmdir()
                    replacement.parent.parent.rmdir()
                    root.rmdir()
                    moved.rename(root)
                return _expected_report()

            with mock.patch.object(
                toolchain_manifest_contract,
                "_build_and_run_contract",
                side_effect=swap_root_back,
            ):
                report = toolchain_manifest_contract.verify_with_contract(
                    root, output, execution_manifest
                )
        self.assertEqual(report, _expected_report())

    @unittest.skipIf(os.name == "nt", "POSIX hard-link semantics are required")
    def test_pinned_reader_rechecks_replaced_wildcard_directories(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            watched = root / "toolchain/tests"
            watched.mkdir(parents=True)
            original_file = watched / "kept.h"
            original_file.write_bytes(b"unchanged\n")
            moved = root / "toolchain/original-tests"
            replacement = root / "toolchain/tests"
            replacement_file = replacement / "kept.h"
            added_file = replacement / "added.h"
            with toolchain_manifest_contract.artifact_size_policy._PinnedRepository(
                root
            ) as reader:
                reader.directory_snapshot("toolchain/tests")
                capture, issue = reader.capture(
                    "toolchain/tests/kept.h", read_payload=True
                )
                self.assertIsNone(issue)
                self.assertIsNotNone(capture)
                watched.rename(moved)
                replacement.mkdir()
                os.link(moved / "kept.h", replacement_file)
                added_file.write_bytes(b"new member\n")
                try:
                    with self.assertRaisesRegex(
                        toolchain_manifest_contract.artifact_size_policy.SizePolicyError,
                        "toolchain/tests changed",
                    ):
                        reader.require_unchanged()
                finally:
                    added_file.unlink()
                    replacement_file.unlink()
                    replacement.rmdir()
                    moved.rename(watched)
                reader.require_unchanged()

    def test_verify_rejects_noninteger_report_counts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output, _manifest, _observations = _write_publication(root)
            execution_manifest = root / "execution.json"
            execution_manifest.write_text("{}\n", encoding="ascii")
            wrong = _expected_report()
            wrong["artifact_count"] = 21.0
            with mock.patch.object(
                toolchain_manifest_contract,
                "_build_and_run_contract",
                return_value=wrong,
            ):
                with self.assertRaisesRegex(
                    toolchain_manifest_contract.ToolchainManifestContractError,
                    "returned invalid counts",
                ):
                    toolchain_manifest_contract.verify_with_contract(
                        root, output, execution_manifest
                    )

    def test_cli_reports_the_checked_cupid_contract_result(self):
        with mock.patch.object(
            toolchain_manifest_contract,
            "verify_with_contract",
            return_value=_expected_report(),
        ) as verify:
            with mock.patch("sys.stdout.write") as write:
                result = toolchain_manifest_contract.main(
                    [
                        "verify",
                        "--root",
                        ".",
                        "--output",
                        "toolchain/build/cupidc-contracts",
                        "--execution-manifest",
                        "bootstrap/seeds/i386-windows/manifest.json",
                        "--timeout",
                        "91",
                    ]
                )
        self.assertEqual(result, 0)
        verify.assert_called_once_with(
            Path("."),
            Path("toolchain/build/cupidc-contracts"),
            Path("bootstrap/seeds/i386-windows/manifest.json"),
            timeout=91,
        )
        write.assert_called_once_with(
            "Cupid Toolchain manifest: ok (22 artifacts)\n"
        )

    def test_cli_reports_a_controlled_failure(self):
        error = toolchain_manifest_contract.ToolchainManifestContractError(
            "manifest snapshot changed"
        )
        with mock.patch.object(
            toolchain_manifest_contract,
            "verify_with_contract",
            side_effect=error,
        ):
            with mock.patch("sys.stderr.write") as write:
                result = toolchain_manifest_contract.main(
                    [
                        "verify",
                        "--root",
                        ".",
                        "--output",
                        "toolchain/build/cupidc-contracts",
                        "--execution-manifest",
                        "bootstrap/seeds/i386-windows/manifest.json",
                    ]
                )
        self.assertEqual(result, 1)
        write.assert_called_once_with(
            "Toolchain manifest verification failed: "
            "manifest snapshot changed\n"
        )

    def test_cli_reports_an_outside_repository_path_without_a_traceback(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            outside = root.parent / "outside-toolchain-publication"
            with mock.patch("sys.stderr.write") as write:
                result = toolchain_manifest_contract.main(
                    [
                        "verify",
                        "--root",
                        str(root),
                        "--output",
                        str(outside),
                        "--execution-manifest",
                        str(root / "execution.json"),
                    ]
                )
        self.assertEqual(result, 1)
        self.assertTrue(
            write.call_args.args[0].startswith(
                "Toolchain manifest verification failed:"
            )
        )

    def test_cli_reports_a_malformed_live_seed_without_a_traceback(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            output, _manifest, _observations = _write_publication(root)
            seed = root / "bootstrap/seeds/i386-linux/manifest.json"
            seed.parent.mkdir(parents=True)
            seed.write_text(
                json.dumps(
                    {
                        "artifacts": [],
                        "build_plan": {"sources": []},
                        "schema": "cupid.bootstrap-seed.v1",
                    }
                ),
                encoding="ascii",
            )
            execution_manifest = root / "execution.json"
            execution_manifest.write_text("{}\n", encoding="ascii")
            with mock.patch.object(
                toolchain_manifest_contract,
                "_capture_live_manifest_closure",
                side_effect=REAL_CAPTURE_LIVE_CLOSURE,
            ):
                with mock.patch.object(
                    toolchain_manifest_contract.cupidc_toolchain_contracts,
                    "_contract_input_paths",
                    return_value=(),
                ):
                    with mock.patch("sys.stderr.write") as write:
                        result = toolchain_manifest_contract.main(
                            [
                                "verify",
                                "--root",
                                str(root),
                                "--output",
                                str(output),
                                "--execution-manifest",
                                str(execution_manifest),
                            ]
                        )
        self.assertEqual(result, 1)
        message = write.call_args.args[0]
        self.assertTrue(
            message.startswith("Toolchain manifest verification failed:")
        )
        self.assertNotIn("Traceback", message)


if __name__ == "__main__":
    unittest.main()

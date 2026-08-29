import hashlib
import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path, PurePosixPath
from unittest import mock

from tools import artifact_size_contract


REPO_ROOT = Path(__file__).resolve().parents[1]
REAL_CAPTURE_CHECKED_SEED = artifact_size_contract._capture_checked_seed
SEED_NAMES = (
    "cupidasm",
    "cupidbuild",
    "cupidc",
    "cupiddis",
    "cupidld",
    "cupidobj",
)
FIXED_OWNERS = {
    "boot/boot.bin": "CupidASM",
    "bootstrap/seeds/i386-windows/cupidasm.exe": "CupidASM",
    "bootstrap/seeds/i386-windows/cupidbuild.exe": "CupidBuild",
    "bootstrap/seeds/i386-windows/cupidc.exe": "CupidC",
    "bootstrap/seeds/i386-windows/cupiddis.exe": "CupidDis",
    "bootstrap/seeds/i386-windows/cupidld.exe": "CupidLD",
    "bootstrap/seeds/i386-windows/cupidobj.exe": "CupidObj",
    "kernel/kernel.bin": "CupidObj",
    "kernel/kernel.elf": "CupidLD",
    "kernel/kernel.elf.pass1": "CupidLD",
}
SEED_OWNERS = {
    "cupidasm": "CupidASM",
    "cupidbuild": "CupidBuild",
    "cupidc": "CupidC",
    "cupiddis": "CupidDis",
    "cupidld": "CupidLD",
    "cupidobj": "CupidObj",
}
EXPECTED_BUILD_INPUTS = (
    "Makefile",
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
    "toolchain/hosted/i386-windows/runtime.cc",
    "toolchain/hosted/i386-windows/tool_start.asm",
    "toolchain/tests/artifact_size_policy_contract.cc",
    "tools/artifact_size_contract.py",
    "tools/artifact_size_policy.py",
    "tools/bootstrap_toolchain.py",
)


class ArtifactSizeContractRunnerTests(unittest.TestCase):
    def setUp(self):
        def capture_checked_seed(reader, _logical_manifest):
            logical_paths = (
                artifact_size_contract.WINDOWS_CHECKED_MANIFEST,
                *(
                    "bootstrap/seeds/i386-windows/" + name
                    for name in artifact_size_contract.WINDOWS_CHECKED_FILES
                ),
            )
            return tuple(
                (
                    logical,
                    artifact_size_contract.artifact_size_policy._required_capture(
                        reader, logical, "checked seed input"
                    ),
                )
                for logical in logical_paths
            )

        def capture_manifest(reader, logical_manifest):
            payload = artifact_size_contract.artifact_size_policy._required_capture(
                reader, logical_manifest, "checked seed manifest"
            )
            return ((logical_manifest, payload),)

        patcher = mock.patch.object(
            artifact_size_contract,
            "_capture_checked_seed",
            side_effect=capture_checked_seed,
        )
        patcher.start()
        self.addCleanup(patcher.stop)
        execution_patcher = mock.patch.object(
            artifact_size_contract,
            "_capture_execution_seed",
            side_effect=capture_manifest,
        )
        execution_patcher.start()
        self.addCleanup(execution_patcher.stop)

    def test_contract_build_input_closure_is_exact(self):
        self.assertEqual(
            artifact_size_contract.BUILD_INPUTS,
            EXPECTED_BUILD_INPUTS,
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
                    str(REPO_ROOT / "tools/artifact_size_contract.py"),
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

    def test_checked_windows_seed_capture_is_complete_and_verified(self):
        with artifact_size_contract.artifact_size_policy._PinnedRepository(
            REPO_ROOT
        ) as reader:
            captures = REAL_CAPTURE_CHECKED_SEED(
                reader, artifact_size_contract.WINDOWS_CHECKED_MANIFEST
            )
            reader.require_unchanged()

        self.assertEqual(
            {PurePosixPath(logical).name for logical, _payload in captures},
            {"manifest.json", *artifact_size_contract.WINDOWS_CHECKED_FILES},
        )

    def test_windows_execution_requires_the_checked_manifest(self):
        checked_seed = (("checked/manifest.json", b"checked"),)
        with self.assertRaisesRegex(
            artifact_size_contract.ArtifactSizeContractError,
            "Windows execution seed is not the checked Windows seed",
        ):
            artifact_size_contract._select_execution_seed(
                mock.sentinel.reader,
                "checked/manifest.json",
                checked_seed,
                "alternate/manifest.json",
                windows=True,
            )
        self.assertIs(
            artifact_size_contract._select_execution_seed(
                mock.sentinel.reader,
                "checked/manifest.json",
                checked_seed,
                "checked/manifest.json",
                windows=True,
            ),
            checked_seed,
        )

    def write_fixture(self, root: Path):
        seed_directory = root / "bootstrap/seeds/i386-linux"
        seed_directory.mkdir(parents=True)
        sizes = {
            "boot/boot.bin": 1,
            "bootstrap/seeds/i386-windows/cupidasm.exe": 10,
            "bootstrap/seeds/i386-windows/cupidbuild.exe": 11,
            "bootstrap/seeds/i386-windows/cupidc.exe": 12,
            "bootstrap/seeds/i386-windows/cupiddis.exe": 13,
            "bootstrap/seeds/i386-windows/cupidld.exe": 14,
            "bootstrap/seeds/i386-windows/cupidobj.exe": 15,
            "kernel/kernel.bin": 7,
            "kernel/kernel.elf": 8,
            "kernel/kernel.elf.pass1": 9,
        }
        for index, name in enumerate(SEED_NAMES, 2):
            sizes[f"bootstrap/seeds/i386-linux/{name}.elf"] = index
        for logical, size in sizes.items():
            path = root / logical
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b"x" * size)
        manifest = seed_directory / "manifest.json"
        manifest.write_text(
            json.dumps(
                {
                    "artifacts": [
                        {
                            "file": f"{name}.elf",
                            "name": name,
                            "producer": name in {"cupidasm", "cupidc", "cupidld"},
                            "sha256": hashlib.sha256(
                                name.encode("ascii")
                            ).hexdigest(),
                            "size": sizes[
                                f"bootstrap/seeds/i386-linux/{name}.elf"
                            ],
                        }
                        for name in SEED_NAMES
                    ],
                    "provenance": {
                        "source_revision": "1" * 40,
                        "source_snapshot_sha256": "2" * 64,
                    },
                    "schema": "cupid.bootstrap-seed.v2",
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
            newline="\n",
        )
        linux_manifest_digest = hashlib.sha256(manifest.read_bytes()).hexdigest()
        windows_manifest = root / artifact_size_contract.WINDOWS_CHECKED_MANIFEST
        windows_manifest.write_text(
            json.dumps(
                {
                    "artifacts": [
                        {
                            "file": f"{name}.exe",
                            "name": name,
                            "producer": name
                            in {"cupidasm", "cupidc", "cupidld"},
                            "sha256": hashlib.sha256(
                                (b"x" * sizes[
                                    "bootstrap/seeds/i386-windows/"
                                    f"{name}.exe"
                                ])
                            ).hexdigest(),
                            "size": sizes[
                                "bootstrap/seeds/i386-windows/"
                                f"{name}.exe"
                            ],
                        }
                        for name in SEED_NAMES
                    ],
                    "provenance": {
                        "artifact_generation": (
                            "paired-stage-four-six-tool-native-windows"
                        ),
                        "fixed_point_command": (
                            "make bootstrap-windows-from-seed"
                        ),
                        "fixed_point_result": "pass",
                        "linux_candidate_build_plan_sha256": (
                            "52dd857bcb74e079e7e2eec45eaa90a0a0838ad2f4e817be"
                            "bc35c9904efbecbd"
                        ),
                        "native_build_plan_sha256": (
                            "f9dce66230a693de9d9d0e60127a4a6c44ea465989f381c9"
                            "95086bfe723cff14"
                        ),
                        "parent_execution_seed_manifest_sha256": (
                            "751e1d7787a4be08e4e86814bbb7473979fe2eb8a3292bae"
                            "d0241967f772eaef"
                        ),
                        "parent_execution_seed_source_revision": (
                            "a17c9465911da41d59b7ada71733d36c39faa5ea"
                        ),
                        "parent_plan_seed_manifest_sha256": (
                            "b6e34a2e18dd18aba91c6358116eafde39953566efeadb22"
                            "4575ac8c13ab2c1b"
                        ),
                        "parent_plan_seed_source_revision": (
                            "a17c9465911da41d59b7ada71733d36c39faa5ea"
                        ),
                        "plan_seed_manifest_sha256": linux_manifest_digest,
                        "producer_lineage": {
                            "assembly": (
                                "native stage-three CupidASM from the checked "
                                "i386 Windows bootstrap"
                            ),
                            "c": (
                                "native stage-three CupidC from the checked "
                                "i386 Windows bootstrap"
                            ),
                            "link": (
                                "native stage-three CupidLD from the checked "
                                "i386 Windows bootstrap"
                            ),
                        },
                        "source_input_count": 58,
                        "source_revision": "1" * 40,
                        "source_snapshot_sha256": "2" * 64,
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
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
            newline="\n",
        )
        owners = dict(FIXED_OWNERS)
        owners.update(
            {
                f"bootstrap/seeds/i386-linux/{name}.elf": owner
                for name, owner in SEED_OWNERS.items()
            }
        )
        policy = root / "bootstrap/artifact-size-policy.json"
        policy.write_text(
            json.dumps(
                {
                    "artifacts": [
                        {
                            "exact_bytes": sizes[path],
                            "path": path,
                            "producer": owners[path],
                            "reason": "fixture lock",
                        }
                        for path in sorted(owners)
                    ],
                    "schema": "cupid.artifact-size-policy.v1",
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
            newline="\n",
        )
        for logical in artifact_size_contract.BUILD_INPUTS:
            path = root.joinpath(*PurePosixPath(logical).parts)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(f"fixture build input: {logical}\n".encode("ascii"))
        return policy, manifest, sizes

    def decode_request(self, request: bytes):
        self.assertEqual(request[:8], b"CUPSIZE2")
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

        policy = take_bytes()
        manifest_path = take_bytes().decode("ascii")
        manifest = take_bytes()
        linux_manifest_digest = take_bytes().decode("ascii")
        windows_manifest_path = take_bytes().decode("ascii")
        windows_manifest = take_bytes()
        windows_observations = []
        for _ in range(take_u32()):
            windows_observations.append(
                (
                    take_bytes().decode("ascii"),
                    take_u32(),
                    take_u64(),
                    take_bytes().decode("ascii"),
                )
            )
        observations = []
        for _ in range(take_u32()):
            observations.append(
                (take_bytes().decode("ascii"), take_u32(), take_u64())
            )
        self.assertEqual(offset, len(request))
        return (
            policy,
            manifest_path,
            manifest,
            linux_manifest_digest,
            windows_manifest_path,
            windows_manifest,
            windows_observations,
            observations,
        )

    def test_verify_with_contract_binds_raw_inputs_and_exact_observations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, manifest, sizes = self.write_fixture(root)
            captured = {}

            def run_contract(request, timeout, build_inputs, execution_seed):
                captured["request"] = request
                captured["timeout"] = timeout
                captured["build_inputs"] = build_inputs
                captured["execution_seed"] = execution_seed
                return {
                    "artifact_count": 16,
                    "schema": "cupid.artifact-size-verification.v1",
                    "total_exact_bytes": sum(sizes.values()),
                }

            with mock.patch.object(
                artifact_size_contract,
                "_build_and_run_contract",
                side_effect=run_contract,
            ):
                report = artifact_size_contract.verify_with_contract(
                    root,
                    policy,
                    manifest,
                    manifest,
                    manifest,
                    timeout=37,
                )

            (
                raw_policy,
                manifest_path,
                raw_manifest,
                linux_manifest_digest,
                windows_manifest_path,
                raw_windows_manifest,
                windows_observations,
                observations,
            ) = self.decode_request(captured["request"])
            self.assertEqual(raw_policy, policy.read_bytes())
            self.assertEqual(
                manifest_path,
                "bootstrap/seeds/i386-linux/manifest.json",
            )
            self.assertEqual(raw_manifest, manifest.read_bytes())
            self.assertEqual(
                linux_manifest_digest,
                hashlib.sha256(manifest.read_bytes()).hexdigest(),
            )
            windows_manifest = (
                root / artifact_size_contract.WINDOWS_CHECKED_MANIFEST
            )
            self.assertEqual(
                windows_manifest_path,
                artifact_size_contract.WINDOWS_CHECKED_MANIFEST,
            )
            self.assertEqual(raw_windows_manifest, windows_manifest.read_bytes())
            self.assertEqual(
                windows_observations,
                [
                    (
                        f"bootstrap/seeds/i386-windows/{name}",
                        1,
                        sizes[f"bootstrap/seeds/i386-windows/{name}"],
                        hashlib.sha256(
                            b"x"
                            * sizes[f"bootstrap/seeds/i386-windows/{name}"]
                        ).hexdigest(),
                    )
                    for name in artifact_size_contract.WINDOWS_CHECKED_FILES
                ],
            )
            self.assertEqual(
                observations,
                [(path, 1, sizes[path]) for path in sorted(sizes)],
            )
            self.assertEqual(captured["timeout"], 37)
            self.assertEqual(
                tuple(logical for logical, _payload in captured["build_inputs"]),
                artifact_size_contract.BUILD_INPUTS,
            )
            if os.name == "nt":
                self.assertEqual(
                    tuple(
                        logical
                        for logical, _payload in captured["execution_seed"]
                    ),
                    (
                        artifact_size_contract.WINDOWS_CHECKED_MANIFEST,
                        *(
                            "bootstrap/seeds/i386-windows/" + name
                            for name in artifact_size_contract.WINDOWS_CHECKED_FILES
                        ),
                    ),
                )
            else:
                self.assertEqual(
                    captured["execution_seed"],
                    ((
                        "bootstrap/seeds/i386-linux/manifest.json",
                        manifest.read_bytes(),
                    ),),
                )
            self.assertEqual(report["artifact_count"], 16)

    def test_verify_with_contract_rejects_a_report_that_differs_from_oracle(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, manifest, _ = self.write_fixture(root)
            wrong = {
                "artifact_count": 16,
                "schema": "cupid.artifact-size-verification.v1",
                "total_exact_bytes": 0,
            }
            with mock.patch.object(
                artifact_size_contract,
                "_build_and_run_contract",
                return_value=wrong,
            ):
                with self.assertRaisesRegex(
                    artifact_size_contract.ArtifactSizeContractError,
                    "differs from the independent Python oracle",
                ):
                    artifact_size_contract.verify_with_contract(
                        root,
                        policy,
                        manifest,
                        manifest,
                        manifest,
                    )

    def test_verify_with_contract_rejects_live_artifact_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, manifest, sizes = self.write_fixture(root)

            def change_live_artifact(
                _request, _timeout, _build_inputs, _execution_seed
            ):
                (root / "boot/boot.bin").write_bytes(b"changed")
                return {
                    "artifact_count": 16,
                    "schema": "cupid.artifact-size-verification.v1",
                    "total_exact_bytes": sum(sizes.values()),
                }

            with mock.patch.object(
                artifact_size_contract,
                "_build_and_run_contract",
                side_effect=change_live_artifact,
            ):
                with self.assertRaisesRegex(
                    artifact_size_contract.ArtifactSizeContractError,
                    "changed while the Cupid contract ran",
                ):
                    artifact_size_contract.verify_with_contract(
                        root,
                        policy,
                        manifest,
                        manifest,
                        manifest,
                    )

    def test_verify_with_contract_rejects_same_size_checked_manifest_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, manifest, sizes = self.write_fixture(root)
            checked_manifest = (
                root / artifact_size_contract.WINDOWS_CHECKED_MANIFEST
            )
            original = checked_manifest.read_bytes()

            def capture_checked_seed(reader, _logical_manifest):
                logical_paths = (
                    artifact_size_contract.WINDOWS_CHECKED_MANIFEST,
                    *(
                        "bootstrap/seeds/i386-windows/" + name
                        for name in artifact_size_contract.WINDOWS_CHECKED_FILES
                    ),
                )
                return tuple(
                    (
                        logical,
                        artifact_size_contract.artifact_size_policy._required_capture(
                            reader, logical, "checked seed input"
                        ),
                    )
                    for logical in logical_paths
                )

            def change_checked_manifest(
                _request, _timeout, _build_inputs, _execution_seed
            ):
                checked_manifest.write_bytes(b" " * len(original))
                return {
                    "artifact_count": 16,
                    "schema": "cupid.artifact-size-verification.v1",
                    "total_exact_bytes": sum(sizes.values()),
                }

            with (
                mock.patch.object(
                    artifact_size_contract,
                    "_capture_checked_seed",
                    side_effect=capture_checked_seed,
                ),
                mock.patch.object(
                    artifact_size_contract,
                    "_build_and_run_contract",
                    side_effect=change_checked_manifest,
                ),
            ):
                with self.assertRaisesRegex(
                    artifact_size_contract.ArtifactSizeContractError,
                    "checked Windows seed changed while the Cupid contract ran",
                ):
                    artifact_size_contract.verify_with_contract(
                        root,
                        policy,
                        manifest,
                        manifest,
                        manifest,
                    )

    def test_verify_with_contract_rejects_same_metadata_build_input_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, manifest, sizes = self.write_fixture(root)

            def change_build_input(
                _request, _timeout, build_inputs, _execution_seed
            ):
                logical, payload = build_inputs[0]
                path = root.joinpath(*PurePosixPath(logical).parts)
                before = path.stat()
                path.write_bytes(b"z" * len(payload))
                os.utime(
                    path,
                    ns=(before.st_atime_ns, before.st_mtime_ns),
                )
                return {
                    "artifact_count": 16,
                    "schema": "cupid.artifact-size-verification.v1",
                    "total_exact_bytes": sum(sizes.values()),
                }

            with mock.patch.object(
                artifact_size_contract,
                "_build_and_run_contract",
                side_effect=change_build_input,
            ):
                with self.assertRaisesRegex(
                    artifact_size_contract.ArtifactSizeContractError,
                    "contract build input changed while the Cupid contract ran",
                ):
                    artifact_size_contract.verify_with_contract(
                        root,
                        policy,
                        manifest,
                        manifest,
                        manifest,
                    )

    @unittest.skipIf(
        os.name == "nt",
        "Windows execution reuses the separately checked Windows seed",
    )
    def test_verify_with_contract_rejects_same_metadata_execution_seed_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, manifest, sizes = self.write_fixture(root)
            execution = root / "bootstrap/seeds/i386-linux/execution.json"
            execution.write_bytes(b"captured execution seed\n")

            def capture_execution_seed(reader, logical_manifest):
                payload = artifact_size_contract.artifact_size_policy._required_capture(
                    reader, logical_manifest, "execution seed manifest"
                )
                return ((logical_manifest, payload),)

            def change_execution_seed(
                _request, _timeout, _build_inputs, execution_seed
            ):
                logical, payload = execution_seed[0]
                path = root.joinpath(*PurePosixPath(logical).parts)
                before = path.stat()
                path.write_bytes(b"z" * len(payload))
                os.utime(
                    path,
                    ns=(before.st_atime_ns, before.st_mtime_ns),
                )
                return {
                    "artifact_count": 16,
                    "schema": "cupid.artifact-size-verification.v1",
                    "total_exact_bytes": sum(sizes.values()),
                }

            with (
                mock.patch.object(
                    artifact_size_contract,
                    "_capture_execution_seed",
                    side_effect=capture_execution_seed,
                ),
                mock.patch.object(
                    artifact_size_contract,
                    "_build_and_run_contract",
                    side_effect=change_execution_seed,
                ),
            ):
                with self.assertRaisesRegex(
                    artifact_size_contract.ArtifactSizeContractError,
                    "execution seed changed while the Cupid contract ran",
                ):
                    artifact_size_contract.verify_with_contract(
                        root,
                        policy,
                        manifest,
                        manifest,
                        execution,
                    )

    @unittest.skipIf(
        os.name == "nt",
        "Windows pinned handles already deny atomic replacement",
    )
    def test_verify_with_contract_rejects_atomic_artifact_replacement(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, manifest, sizes = self.write_fixture(root)

            def replace_live_artifact(
                _request, _timeout, _build_inputs, _execution_seed
            ):
                original = root / "boot/boot.bin"
                original.rename(root / "boot/original.bin")
                replacement = root / "boot/replacement.bin"
                replacement.write_bytes(b"y")
                replacement.rename(original)
                return {
                    "artifact_count": 16,
                    "schema": "cupid.artifact-size-verification.v1",
                    "total_exact_bytes": sum(sizes.values()),
                }

            with mock.patch.object(
                artifact_size_contract,
                "_build_and_run_contract",
                side_effect=replace_live_artifact,
            ):
                with self.assertRaisesRegex(
                    artifact_size_contract.ArtifactSizeContractError,
                    "changed while the Cupid contract ran",
                ):
                    artifact_size_contract.verify_with_contract(
                        root,
                        policy,
                        manifest,
                        manifest,
                        manifest,
                    )

    @unittest.skipIf(
        os.name == "nt",
        "Windows pinned handles already deny directory replacement",
    )
    def test_verify_with_contract_rejects_artifact_parent_replacement(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, manifest, sizes = self.write_fixture(root)

            def replace_artifact_parent(
                _request, _timeout, _build_inputs, _execution_seed
            ):
                (root / "boot").rename(root / "old-boot")
                (root / "boot").mkdir()
                (root / "boot/boot.bin").write_bytes(b"y")
                return {
                    "artifact_count": 16,
                    "schema": "cupid.artifact-size-verification.v1",
                    "total_exact_bytes": sum(sizes.values()),
                }

            with mock.patch.object(
                artifact_size_contract,
                "_build_and_run_contract",
                side_effect=replace_artifact_parent,
            ):
                with self.assertRaisesRegex(
                    artifact_size_contract.ArtifactSizeContractError,
                    "changed while the Cupid contract ran",
                ):
                    artifact_size_contract.verify_with_contract(
                        root,
                        policy,
                        manifest,
                        manifest,
                        manifest,
                    )

    @unittest.skipIf(
        os.name == "nt",
        "Windows pinned handles already deny repository replacement",
    )
    def test_verify_with_contract_rejects_repository_root_replacement(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, manifest, sizes = self.write_fixture(root)
            moved = root.with_name(root.name + "-pinned-original")

            def replace_repository_root(
                _request, _timeout, build_inputs, execution_seed
            ):
                self.assertEqual(
                    tuple(logical for logical, _payload in build_inputs),
                    artifact_size_contract.BUILD_INPUTS,
                )
                self.assertEqual(execution_seed[0][1], manifest.read_bytes())
                root.rename(moved)
                root.mkdir()
                return {
                    "artifact_count": 16,
                    "schema": "cupid.artifact-size-verification.v1",
                    "total_exact_bytes": sum(sizes.values()),
                }

            try:
                with mock.patch.object(
                    artifact_size_contract,
                    "_build_and_run_contract",
                    side_effect=replace_repository_root,
                ):
                    with self.assertRaisesRegex(
                        artifact_size_contract.ArtifactSizeContractError,
                        "repository root changed while the Cupid contract ran",
                    ):
                        artifact_size_contract.verify_with_contract(
                            root,
                            policy,
                            manifest,
                            manifest,
                            manifest,
                        )
            finally:
                if root.exists():
                    root.rmdir()
                if moved.exists():
                    moved.rename(root)

    def test_verify_with_contract_rejects_noninteger_report_counts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, manifest, sizes = self.write_fixture(root)
            report = {
                "artifact_count": 16.0,
                "schema": "cupid.artifact-size-verification.v1",
                "total_exact_bytes": float(sum(sizes.values())),
            }
            with mock.patch.object(
                artifact_size_contract,
                "_build_and_run_contract",
                return_value=report,
            ):
                with self.assertRaisesRegex(
                    artifact_size_contract.ArtifactSizeContractError,
                    "invalid field types",
                ):
                    artifact_size_contract.verify_with_contract(
                        root,
                        policy,
                        manifest,
                        manifest,
                        manifest,
                    )

    def test_cli_reports_the_checked_cupid_contract_result(self):
        report = {
            "artifact_count": 16,
            "schema": "cupid.artifact-size-verification.v1",
            "total_exact_bytes": 42,
        }
        with mock.patch.object(
            artifact_size_contract,
            "verify_with_contract",
            return_value=report,
        ) as verify:
            with mock.patch("sys.stdout.write") as write:
                result = artifact_size_contract.main(
                    [
                        "verify",
                        "--root",
                        ".",
                        "--policy",
                        "bootstrap/artifact-size-policy.json",
                        "--seed-manifest",
                        "bootstrap/seeds/i386-linux/manifest.json",
                        "--checked-manifest",
                        "bootstrap/seeds/i386-windows/manifest.json",
                        "--execution-manifest",
                        "bootstrap/seeds/i386-windows/manifest.json",
                        "--timeout",
                        "91",
                    ]
                )
        self.assertEqual(result, 0)
        verify.assert_called_once_with(
            Path("."),
            Path("bootstrap/artifact-size-policy.json"),
            Path("bootstrap/seeds/i386-linux/manifest.json"),
            Path("bootstrap/seeds/i386-windows/manifest.json"),
            Path("bootstrap/seeds/i386-windows/manifest.json"),
            timeout=91,
        )
        write.assert_called_once_with(
            "Cupid artifact sizes: ok (16 exact artifacts)\n"
        )

    def test_cli_preserves_multiline_failure_format(self):
        error = artifact_size_contract.artifact_size_policy.SizePolicyError(
            "\n- first failure\n- second failure"
        )
        with mock.patch.object(
            artifact_size_contract,
            "verify_with_contract",
            side_effect=error,
        ):
            with mock.patch("sys.stderr.write") as write:
                result = artifact_size_contract.main(
                    [
                        "verify",
                        "--root",
                        ".",
                        "--policy",
                        "policy.json",
                        "--seed-manifest",
                        "manifest.json",
                        "--checked-manifest",
                        "checked.json",
                        "--execution-manifest",
                        "execution.json",
                    ]
                )
        self.assertEqual(result, 1)
        write.assert_called_once_with(
            "artifact size verification failed:\n"
            "- first failure\n"
            "- second failure\n"
        )


if __name__ == "__main__":
    unittest.main()

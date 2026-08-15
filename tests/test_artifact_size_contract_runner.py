import json
import os
import struct
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import artifact_size_contract


SEED_NAMES = ("cupidasm", "cupidc", "cupiddis", "cupidld", "cupidobj")
FIXED_OWNERS = {
    "boot/boot.bin": "CupidASM",
    "kernel/kernel.bin": "CupidObj",
    "kernel/kernel.elf": "CupidLD",
    "kernel/kernel.elf.pass1": "CupidLD",
}
SEED_OWNERS = {
    "cupidasm": "CupidASM",
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
    def test_contract_build_input_closure_is_exact(self):
        self.assertEqual(
            artifact_size_contract.BUILD_INPUTS,
            EXPECTED_BUILD_INPUTS,
        )

    def write_fixture(self, root: Path):
        seed_directory = root / "bootstrap/seeds/i386-linux"
        seed_directory.mkdir(parents=True)
        sizes = {
            "boot/boot.bin": 1,
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
                            "size": sizes[
                                f"bootstrap/seeds/i386-linux/{name}.elf"
                            ],
                        }
                        for name in SEED_NAMES
                    ],
                    "schema": "cupid.bootstrap-seed.v1",
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
        return policy, manifest, sizes

    def decode_request(self, request: bytes):
        self.assertEqual(request[:8], b"CUPSIZE1")
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
        observations = []
        for _ in range(take_u32()):
            observations.append(
                (take_bytes().decode("ascii"), take_u32(), take_u64())
            )
        self.assertEqual(offset, len(request))
        return policy, manifest_path, manifest, observations

    def test_verify_with_contract_binds_raw_inputs_and_exact_observations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, manifest, sizes = self.write_fixture(root)
            captured = {}

            def run_contract(_root, execution_manifest, request, timeout):
                captured["manifest"] = execution_manifest
                captured["request"] = request
                captured["timeout"] = timeout
                return {
                    "artifact_count": 9,
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
                    timeout=37,
                )

            raw_policy, manifest_path, raw_manifest, observations = (
                self.decode_request(captured["request"])
            )
            self.assertEqual(raw_policy, policy.read_bytes())
            self.assertEqual(
                manifest_path,
                "bootstrap/seeds/i386-linux/manifest.json",
            )
            self.assertEqual(raw_manifest, manifest.read_bytes())
            self.assertEqual(
                observations,
                [(path, 1, sizes[path]) for path in sorted(sizes)],
            )
            self.assertEqual(captured["manifest"], manifest)
            self.assertEqual(captured["timeout"], 37)
            self.assertEqual(report["artifact_count"], 9)

    def test_verify_with_contract_rejects_a_report_that_differs_from_oracle(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, manifest, _ = self.write_fixture(root)
            wrong = {
                "artifact_count": 9,
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
                    )

    def test_verify_with_contract_rejects_live_artifact_drift(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, manifest, sizes = self.write_fixture(root)

            def change_live_artifact(_root, _manifest, _request, _timeout):
                (root / "boot/boot.bin").write_bytes(b"changed")
                return {
                    "artifact_count": 9,
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
                    )

    @unittest.skipIf(
        os.name == "nt",
        "Windows pinned handles already deny atomic replacement",
    )
    def test_verify_with_contract_rejects_atomic_artifact_replacement(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, manifest, sizes = self.write_fixture(root)

            def replace_live_artifact(_root, _manifest, _request, _timeout):
                original = root / "boot/boot.bin"
                original.rename(root / "boot/original.bin")
                replacement = root / "boot/replacement.bin"
                replacement.write_bytes(b"y")
                replacement.rename(original)
                return {
                    "artifact_count": 9,
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
                    )

    @unittest.skipIf(
        os.name == "nt",
        "Windows pinned handles already deny directory replacement",
    )
    def test_verify_with_contract_rejects_artifact_parent_replacement(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, manifest, sizes = self.write_fixture(root)

            def replace_artifact_parent(_root, _manifest, _request, _timeout):
                (root / "boot").rename(root / "old-boot")
                (root / "boot").mkdir()
                (root / "boot/boot.bin").write_bytes(b"y")
                return {
                    "artifact_count": 9,
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
                    )

    def test_verify_with_contract_rejects_noninteger_report_counts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, manifest, sizes = self.write_fixture(root)
            report = {
                "artifact_count": 9.0,
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
                    )

    def test_cli_reports_the_checked_cupid_contract_result(self):
        report = {
            "artifact_count": 9,
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
            timeout=91,
        )
        write.assert_called_once_with(
            "Cupid artifact sizes: ok (9 exact artifacts)\n"
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

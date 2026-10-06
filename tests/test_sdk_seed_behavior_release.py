"""The SDK must pass reviewed authority without taking over native comparisons."""
import contextlib
import hashlib
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from tools import bootstrap_stage_release as release
from tools import bootstrap_toolchain as seed
from tools import cupidc_toolchain_contracts as sdk
from tools import seed_release_identity as pins


ROOT = Path(__file__).resolve().parents[1]


class SDKSeedBehaviorReleaseTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix="cupid-sdk-authority-test-")
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        (self.root / "toolchain").mkdir()
        shutil.copytree(ROOT / "bootstrap/seeds/i386-linux", self.root / "seed")
        self.manifest = self.root / "seed/manifest.json"
        self.path = self.root / "release.json"
        self.payload = pins.encode_release_identity()
        self.path.write_bytes(self.payload)
        self.output = self.root / "toolchain/build/cupidc-contracts"

    def test_author_bootstrap_forwards_request_and_preserves_native_comparison_owner(self):
        request = release.capture_seed_behavior_release(self.path, seed.verify_seed_inputs(self.manifest))
        with mock.patch.object(seed, "_bootstrap_from_seed_with_policy", return_value={"marker": "handoff"}) as bootstrap:
            self.assertEqual(seed._bootstrap_for_manifest_author(self.manifest, self.root, self.output,
                                                                release_request=request), {"marker": "handoff"})
        self.assertIs(bootstrap.call_args.kwargs["release_request"], request)
        self.assertIs(bootstrap.call_args.kwargs["compare_fixed_point"], False)

    def test_sdk_passes_the_real_captured_request_before_any_stage_build(self):
        observed = []

        def bootstrap(_manifest, _root, _output, **options):
            request = options["release_request"]
            self.assertIsInstance(request, release.SeedBehaviorRequest)
            self.assertEqual(request.payload, self.payload)
            request.require_live()
            observed.append(request)
            raise seed.BootstrapError("controlled stop after authority handoff")

        with mock.patch.object(sdk, "_contract_input_paths", return_value=()), \
                mock.patch.object(sdk, "_bootstrap_for_manifest_author", side_effect=bootstrap):
            with self.assertRaisesRegex(sdk.ContractError, "controlled stop after authority handoff"):
                sdk.build_contracts(self.root, self.manifest, self.output, behavior_release=self.path)
        self.assertEqual(len(observed), 1)
        self.assertFalse(self.output.exists())
        self.assertEqual(self.path.read_bytes(), self.payload)

    def test_invalid_authority_fails_before_bootstrap_and_preserves_prior_publication(self):
        self.path.write_bytes(self.payload + b"invalid")
        self.output.mkdir(parents=True)
        prior = self.output / "prior.elf"
        prior.write_bytes(b"prior output")
        with mock.patch.object(sdk, "_validate_output_target", return_value=self.output), \
                mock.patch.object(sdk, "_bootstrap_for_manifest_author") as bootstrap:
            with self.assertRaisesRegex(sdk.ContractError, "behavior release"):
                sdk.build_contracts(self.root, self.manifest, self.output, behavior_release=self.path)
        bootstrap.assert_not_called()
        self.assertEqual(prior.read_bytes(), b"prior output")
        self.assertEqual(sorted(path.name for path in self.output.iterdir()), ["prior.elf"])

    def test_seed_drift_fails_before_bootstrap(self):
        path = self.root / "seed/cupidobj.elf"
        path.write_bytes(path.read_bytes() + b"changed")
        with mock.patch.object(sdk, "_bootstrap_for_manifest_author") as bootstrap:
            with self.assertRaisesRegex(sdk.ContractError, "behavior release"):
                sdk.build_contracts(self.root, self.manifest, self.output, behavior_release=self.path)
        bootstrap.assert_not_called()
        self.assertFalse(self.output.exists())

    def test_build_and_ensure_cli_accept_explicit_authority(self):
        for command in ("build", "ensure"):
            with self.subTest(command=command), contextlib.redirect_stderr(io.StringIO()):
                args = sdk._build_parser().parse_args([command, "--root", str(self.root),
                    "--manifest", str(self.manifest), "--output", str(self.output),
                    "--behavior-release", str(self.path)])
                self.assertEqual(args.behavior_release, self.path)

    def test_authority_implementations_belong_to_the_captured_inventory(self):
        paths = {path.relative_to(ROOT).as_posix() for path in sdk._contract_input_paths(ROOT)}
        self.assertEqual(len(paths), 101)
        self.assertTrue({"tools/__init__.py", "tools/bootstrap_user_abi.py",
                         "tools/bootstrap_stage_release.py", "tools/seed_release_identity.py"} <= paths)

    def test_original_author_bootstrap_stays_strict(self):
        with mock.patch.object(seed, "_bootstrap_from_seed_with_policy", return_value={}) as bootstrap:
            seed._bootstrap_for_manifest_author(self.manifest, self.root, self.output)
        self.assertNotIn("release_request", bootstrap.call_args.kwargs)
        self.assertIs(bootstrap.call_args.kwargs["compare_fixed_point"], False)

    def _cached_report(self):
        selected = seed.verify_seed_inputs(self.manifest)
        return {"artifacts": [
            {"path": sdk.TOOL_PUBLIC_NAMES[role], "size": len(data),
             "sha256": hashlib.sha256(data).hexdigest()}
            for role, data in selected.artifact_bytes],
            "bootstrap": {"source_inputs": {"files": {
                "toolchain/hosted/i386-windows/final_path_start.asm": {}}}}}

    def test_cache_requires_the_reviewed_tools_and_forwards_release_on_rebuild(self):
        self.output.mkdir(parents=True)
        report = self._cached_report()
        with mock.patch.object(sdk, "_validate_output_target", return_value=self.output), \
                mock.patch.object(sdk, "verify_publication", return_value=report), \
                mock.patch.object(sdk, "verify_publication_inputs"), \
                mock.patch.object(sdk, "_require_report_manifest"), \
                mock.patch.object(sdk, "build_contracts", return_value={"rebuilt": True}) as build:
            self.assertIs(sdk.ensure_contracts(self.root, self.manifest, self.output,
                                               behavior_release=self.path), report)
            build.assert_not_called()
            report["artifacts"][0]["sha256"] = "0" * 64
            self.assertEqual(sdk.ensure_contracts(self.root, self.manifest, self.output,
                                                  behavior_release=self.path), {"rebuilt": True})
        self.assertEqual(build.call_args.kwargs["behavior_release"], self.path)
        self.assertEqual(self.path.read_bytes(), self.payload)

    def test_cache_rejects_release_drift_after_verification_without_rebuilding(self):
        self.output.mkdir(parents=True)
        prior = self.output / "prior.elf"
        prior.write_bytes(b"prior publication")
        report = self._cached_report()
        with mock.patch.object(sdk, "_validate_output_target", return_value=self.output), \
                mock.patch.object(sdk, "verify_publication", return_value=report), \
                mock.patch.object(sdk, "verify_publication_inputs",
                                  side_effect=lambda *_: self.path.write_bytes(self.payload + b"\n")), \
                mock.patch.object(sdk, "_require_report_manifest"), \
                mock.patch.object(sdk, "build_contracts") as build:
            with self.assertRaisesRegex(sdk.ContractError, "behavior release changed before publication"):
                sdk.ensure_contracts(self.root, self.manifest, self.output, behavior_release=self.path)
        build.assert_not_called()
        self.assertEqual(prior.read_bytes(), b"prior publication")
        self.path.write_bytes(self.payload)
        release.capture_seed_behavior_release(self.path, seed.verify_seed_inputs(self.manifest)).require_live()

    def test_standalone_sdk_resolves_authority_modules_and_rejects_invalid_record(self):
        self.path.write_bytes(b"invalid reviewed release")
        environment = {key: value for key, value in os.environ.items() if key != "PYTHONPATH"}
        result = subprocess.run([sys.executable, str(ROOT / "tools/cupidc_toolchain_contracts.py"),
            "build", "--root", str(self.root), "--manifest", str(self.manifest),
            "--output", str(self.output), "--behavior-release", str(self.path)],
            cwd=self.root, env=environment, capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("behavior release is invalid", result.stderr)
        self.assertNotIn("Traceback", result.stderr)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()

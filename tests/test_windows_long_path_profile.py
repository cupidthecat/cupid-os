"""Carry an exact Windows long-file profile through bootstrap and publication."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

from tools import bootstrap_toolchain as bootstrap
from tools import cupidc_toolchain_contracts as contracts
from tools import artifact_size_policy, toolchain_manifest_contract as verifier

ROOT = Path(__file__).resolve().parents[1]
LINUX = ROOT / "bootstrap/seeds/i386-linux/manifest.json"
WINDOWS = ROOT / "bootstrap/seeds/i386-windows/manifest.json"
LONG_PLAN = "5647e926c96a50be0d5c7089a04ac3259e5e8c00ad9a32b50d0a78f11c16e3cc"
SHIM = "toolchain/hosted/i386-windows/utf8_long_path_start.asm"


class WindowsLongPathProfileTests(unittest.TestCase):
    def test_exact_import_profile_keeps_count_and_plan_bound(self):
        linux = bootstrap._candidate_build_plan(json.loads(LINUX.read_bytes())["build_plan"])
        plan = bootstrap._windows_build_plan(linux, utf8=True, long_paths=True)
        self.assertEqual(bootstrap._build_plan_sha256(plan), LONG_PLAN)
        self.assertEqual(bootstrap._windows_plan_profile(plan), (True, True, False))
        for name in bootstrap.CANDIDATE_TOOL_NAMES:
            self.assertEqual(bootstrap._promoted_windows_imports(name, LONG_PLAN, 77),
                             bootstrap._windows_utf8_imports(name, long_paths=True))
            for count in (76, 78, True, 77.0, "77"):
                with self.subTest(tool=name, count=count), self.assertRaisesRegex(
                        bootstrap.BootstrapError, "import profile differs"):
                    bootstrap._promoted_windows_imports(name, LONG_PLAN, count)
        with self.assertRaisesRegex(bootstrap.BootstrapError, "unknown.*role"):
            bootstrap._promoted_windows_imports("unknown", LONG_PLAN, 77)

    def test_public_drivers_forward_explicit_selection_and_keep_fixed_point(self):
        seed = bootstrap.verify_seed_inputs(LINUX)
        native = bootstrap.verify_seed_inputs(WINDOWS)
        with mock.patch.object(bootstrap, "freeze_seed_inputs", return_value=seed), mock.patch.object(
                bootstrap, "_bootstrap_from_frozen_seed", return_value={"status": "pass"}) as driver:
            bootstrap.bootstrap_from_seed(LINUX, ROOT, ROOT / "unused-output", windows_long_paths=True)
        self.assertEqual(driver.call_args.kwargs,
                         {"compare_fixed_point": True, "windows_long_paths": True, "windows_user_link_aliases": True})
        with mock.patch.object(bootstrap, "freeze_seed_inputs", side_effect=(native, seed)), mock.patch.object(
                bootstrap, "_bootstrap_windows_from_frozen_seed", return_value={"status": "pass"}) as driver, mock.patch.object(
                bootstrap, "os") as platform:
            platform.name = "nt"
            bootstrap.bootstrap_windows_from_seed(WINDOWS, LINUX, ROOT, ROOT / "unused-output", windows_long_paths=True)
        self.assertEqual(driver.call_args.kwargs, {"windows_long_paths": True, "windows_user_link_aliases": True})

    def test_private_drivers_select_matching_frozen_source_inventory(self):
        seed = bootstrap.verify_seed_inputs(LINUX)
        native = bootstrap.verify_seed_inputs(WINDOWS)
        for selected in (False, True):
            for windows in (False, True):
                with self.subTest(windows=windows, selected=selected), tempfile.TemporaryDirectory(dir=ROOT) as temporary, mock.patch.object(
                        bootstrap, "freeze_source_inputs", side_effect=bootstrap.BootstrapError("capture checkpoint")) as capture:
                    output = Path(temporary) / "output"
                    with self.assertRaisesRegex(bootstrap.BootstrapError, "capture checkpoint"):
                        if windows:
                            bootstrap._bootstrap_windows_from_frozen_seed(native, seed, ROOT, output,
                                windows_long_paths=selected)
                        else:
                            bootstrap._bootstrap_from_frozen_seed(seed, ROOT, output,
                                compare_fixed_point=True, windows_long_paths=selected)
                    self.assertEqual(capture.call_args.kwargs,
                                     {"windows_utf8": True, "windows_long_paths": selected, "windows_user_link_aliases": True})
                    self.assertFalse(output.exists())

    def test_invalid_selection_is_rejected_before_input_or_output_mutation(self):
        for value in (1, None, "yes", (), []):
            with self.subTest(value=value), mock.patch.object(bootstrap, "freeze_seed_inputs") as capture:
                for call in (
                    lambda: bootstrap.bootstrap_from_seed(LINUX, ROOT, ROOT / "unused-output", windows_long_paths=value),
                    lambda: bootstrap.bootstrap_windows_from_seed(WINDOWS, LINUX, ROOT, ROOT / "unused-output", windows_long_paths=value),
                    lambda: contracts.build_contracts(ROOT, LINUX, ROOT / "unused-output", windows_long_paths=value),
                    lambda: contracts.ensure_contracts(ROOT, LINUX, ROOT / "unused-output", windows_long_paths=value),
                ):
                    with self.assertRaisesRegex((bootstrap.BootstrapError, contracts.ContractError), "Boolean"):
                        call()
                capture.assert_not_called()

    def test_behavior_retarget_retains_long_profile_and_installed_parent_pair(self):
        native = bootstrap.verify_seed_inputs(WINDOWS)
        linux = bootstrap._candidate_build_plan(json.loads(LINUX.read_bytes())["build_plan"])
        snapshot = bootstrap.capture_source_snapshot(ROOT, linux, windows_utf8=True, windows_long_paths=True)
        self.assertEqual(len(snapshot), 77)
        self.assertIn(SHIM, snapshot)
        changed = bootstrap._retarget_native_windows_behavior_seed(native, LONG_PLAN, linux, snapshot,
            utf8=True, long_paths=True, parent_plan_seed=bootstrap.verify_seed_inputs(LINUX))
        provenance = changed.manifest["provenance"]
        self.assertEqual(provenance["source_input_count"], 77)
        self.assertEqual(provenance["parent_execution_seed_manifest_sha256"], native.manifest_sha256)
        self.assertEqual(provenance["parent_plan_seed_manifest_sha256"], bootstrap.verify_seed_inputs(LINUX).manifest_sha256)
        self.assertEqual(provenance["native_build_plan_sha256"], LONG_PLAN)
        self.assertEqual(changed.artifact_bytes, native.artifact_bytes)
        with self.assertRaisesRegex(bootstrap.BootstrapError, "parent plan seed is unavailable"):
            bootstrap._retarget_native_windows_behavior_seed(native, LONG_PLAN, linux, snapshot, utf8=True, long_paths=True)
        with self.assertRaisesRegex(bootstrap.BootstrapError, "build plan differs"):
            bootstrap._retarget_native_windows_behavior_seed(native, LONG_PLAN, linux, snapshot, utf8=True)

    def test_publication_driver_passes_selection_to_the_manifest_author(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as temporary, mock.patch.object(
                contracts, "_bootstrap_for_manifest_author", side_effect=bootstrap.BootstrapError("capture checkpoint")) as driver:
            output = Path(temporary) / "cupidc-contracts"
            with self.assertRaisesRegex(contracts.ContractError, "capture checkpoint"):
                contracts.build_contracts(ROOT, LINUX, output, windows_long_paths=True)
            self.assertEqual(driver.call_args.kwargs, {"windows_long_paths": True, "windows_user_link_aliases": True})
            self.assertFalse(output.exists())

    def test_publication_reuse_requires_the_selected_profile(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as temporary:
            output = Path(temporary) / "cupidc-contracts"
            output.mkdir()
            report = {"bootstrap": {"source_inputs": {"files": {SHIM: {}, "toolchain/hosted/i386-windows/final_path_start.asm": {}}}}}
            with mock.patch.object(contracts, "verify_publication", return_value=report), mock.patch.object(
                    contracts, "verify_publication_inputs"), mock.patch.object(contracts, "_require_report_manifest"), mock.patch.object(
                    contracts, "build_contracts", return_value={"rebuilt": True}) as build:
                self.assertIs(contracts.ensure_contracts(ROOT, LINUX, output, windows_long_paths=True), report)
                build.assert_not_called()
                self.assertEqual(contracts.ensure_contracts(ROOT, LINUX, output), {"rebuilt": True})
                self.assertEqual(build.call_args.kwargs, {"windows_user_link_aliases": True})

    def test_observer_captures_only_the_selected_producer_inventory(self):
        linux = bootstrap._candidate_build_plan(json.loads(LINUX.read_bytes())["build_plan"])
        with artifact_size_policy._PinnedRepository(ROOT) as reader:
            short = verifier._bootstrap_input_logical_paths(reader, linux)
            long = verifier._bootstrap_input_logical_paths(reader, linux, windows_long_paths=True)
            self.assertEqual(len(short), 76)
            self.assertEqual(len(long), 77)
            self.assertNotIn(SHIM, short)
            self.assertEqual(set(long) - set(short), {SHIM})
            reader.require_unchanged()

    def test_cli_options_are_explicit_and_default_to_original_profile(self):
        for command, extra in (("bootstrap", []), ("bootstrap-windows", ["--plan-manifest", str(LINUX)])):
            args = [command, "--manifest", str(LINUX), "--root", str(ROOT), "--output", "unused", *extra]
            self.assertFalse(bootstrap._build_parser().parse_args(args).windows_long_paths)
            self.assertTrue(bootstrap._build_parser().parse_args([*args, "--windows-long-paths"]).windows_long_paths)
        for command in ("build", "ensure"):
            args = [command, "--manifest", str(LINUX), "--root", str(ROOT), "--output", "unused"]
            self.assertFalse(contracts._build_parser().parse_args(args).windows_long_paths)
            self.assertTrue(contracts._build_parser().parse_args([*args, "--windows-long-paths"]).windows_long_paths)


if __name__ == "__main__":
    unittest.main()

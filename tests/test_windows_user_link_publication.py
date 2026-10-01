"""Carry the exact directory-alias profile through public bootstrap boundaries."""
import json
import os
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest import mock
from tools import bootstrap_toolchain as bootstrap
from tools import cupidc_toolchain_contracts as contracts
from tools import artifact_size_policy, toolchain_manifest_contract as verifier
ROOT = Path(__file__).resolve().parents[1]
LINUX = ROOT / "bootstrap/seeds/i386-linux/manifest.json"
WINDOWS = ROOT / "bootstrap/seeds/i386-windows/manifest.json"
SHIM = "toolchain/hosted/i386-windows/final_path_start.asm"

class WindowsUserLinkPublicationTests(unittest.TestCase):
    def test_alias_bridge_is_a_publication_and_make_dependency_on_both_hosts(self):
        from tools import build_graph_audit as audit

        paths = {path.relative_to(ROOT).as_posix()
                 for path in contracts._contract_input_paths(ROOT)}
        self.assertTrue(SHIM in paths, "publication omits the alias bridge")
        variables = ("TOOLCHAIN_MANIFEST_PUBLICATION_INPUTS",
                     "TOOLCHAIN_MANIFEST_BOOTSTRAP_INPUTS")
        for host in ("Windows_NT", "Linux"):
            with self.subTest(host=host), mock.patch.object(
                    audit, "CANONICAL_MAKE_VARIABLES", (f"OS={host}",)):
                evaluated = audit._read_evaluated_make_variables(
                    ROOT / "toolchain", "make", variables)
                for name, values in evaluated.items():
                    self.assertIn("hosted/i386-windows/final_path_start.asm", values.split(), name)

    def test_publication_recheck_rejects_same_size_alias_bridge_drift(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as temporary:
            root = Path(temporary) / "source"
            for source in contracts._contract_input_paths(ROOT):
                destination = root / source.relative_to(ROOT)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(source, destination)
            paths = contracts._contract_input_paths(root)
            snapshot = contracts._snapshot_contract_inputs(root, paths)
            self.assertTrue(SHIM in snapshot, "publication snapshot omits the alias bridge")
            contracts._require_inputs_unchanged(root, snapshot)
            bridge = root / SHIM
            previous = bridge.stat()
            raw = bridge.read_bytes()
            bridge.write_bytes(bytes([raw[0] ^ 1]) + raw[1:])
            os.utime(bridge, ns=(previous.st_atime_ns, previous.st_mtime_ns))
            self.assertEqual(bridge.stat().st_size, previous.st_size)
            self.assertEqual(bridge.stat().st_mtime_ns, previous.st_mtime_ns)
            with self.assertRaisesRegex(contracts.ContractError, "contract inputs changed"):
                contracts._require_inputs_unchanged(root, snapshot)

    def test_public_drivers_forward_explicit_selection_and_keep_fixed_point(self):
        seed = bootstrap.verify_seed_inputs(LINUX)
        native = bootstrap.verify_seed_inputs(WINDOWS)
        with mock.patch.object(bootstrap, "freeze_seed_inputs", return_value=seed), mock.patch.object(
                bootstrap, "_bootstrap_from_frozen_seed", return_value={"status": "pass"}) as driver:
            bootstrap.bootstrap_from_seed(LINUX, ROOT, ROOT / "unused-output")
        self.assertEqual(driver.call_args.kwargs,
                         {"compare_fixed_point": True, "windows_long_paths": False,
                          "windows_user_link_aliases": True})
        with mock.patch.object(bootstrap, "freeze_seed_inputs", side_effect=(native, seed)), mock.patch.object(
                bootstrap, "_bootstrap_windows_from_frozen_seed", return_value={"status": "pass"}) as driver, mock.patch.object(
                bootstrap, "os") as platform:
            platform.name = "nt"
            bootstrap.bootstrap_windows_from_seed(WINDOWS, LINUX, ROOT, ROOT / "unused-output")
        self.assertEqual(driver.call_args.kwargs, {"windows_user_link_aliases": True})

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
                                windows_user_link_aliases=selected)
                        else:
                            bootstrap._bootstrap_from_frozen_seed(seed, ROOT, output,
                                compare_fixed_point=True, windows_user_link_aliases=selected)
                    self.assertEqual(capture.call_args.kwargs,
                                     {"windows_utf8": True, "windows_long_paths": False,
                                      **({"windows_user_link_aliases": True} if selected else {})})
                    self.assertFalse(output.exists())

    def test_default_and_long_path_only_drivers_capture_the_required_bridge(self):
        seed = bootstrap.verify_seed_inputs(LINUX)
        native = bootstrap.verify_seed_inputs(WINDOWS)
        for long_paths in (False, True):
            for windows in (False, True):
                with self.subTest(windows=windows, long_paths=long_paths), tempfile.TemporaryDirectory(dir=ROOT) as temporary, mock.patch.object(
                        bootstrap, "freeze_source_inputs", side_effect=bootstrap.BootstrapError("capture checkpoint")) as capture:
                    output = Path(temporary) / "output"
                    with self.assertRaisesRegex(bootstrap.BootstrapError, "capture checkpoint"):
                        if windows:
                            bootstrap._bootstrap_windows_from_frozen_seed(native, seed, ROOT, output,
                                windows_long_paths=long_paths)
                        else:
                            bootstrap._bootstrap_from_frozen_seed(seed, ROOT, output,
                                compare_fixed_point=True, windows_long_paths=long_paths)
                    self.assertEqual(capture.call_args.kwargs,
                        {"windows_utf8": True, "windows_long_paths": long_paths,
                         "windows_user_link_aliases": True})
                    self.assertFalse(output.exists())

    def test_invalid_selection_is_rejected_before_input_or_output_mutation(self):
        for value in (1, None, "yes", (), []):
            with self.subTest(value=value), mock.patch.object(bootstrap, "freeze_seed_inputs") as capture:
                for call in (
                    lambda: bootstrap.bootstrap_from_seed(LINUX, ROOT, ROOT / "unused-output", windows_user_link_aliases=value),
                    lambda: bootstrap.bootstrap_windows_from_seed(WINDOWS, LINUX, ROOT, ROOT / "unused-output", windows_user_link_aliases=value),
                    lambda: contracts.build_contracts(ROOT, LINUX, ROOT / "unused-output", windows_user_link_aliases=value),
                    lambda: contracts.ensure_contracts(ROOT, LINUX, ROOT / "unused-output", windows_user_link_aliases=value),
                ):
                    with self.assertRaisesRegex((bootstrap.BootstrapError, contracts.ContractError), "Boolean"):
                        call()
                capture.assert_not_called()

    def test_publication_driver_passes_selection_to_the_manifest_author(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as temporary, mock.patch.object(
                contracts, "_bootstrap_for_manifest_author", side_effect=bootstrap.BootstrapError("capture checkpoint")) as driver:
            output = Path(temporary) / "cupidc-contracts"
            with self.assertRaisesRegex(contracts.ContractError, "capture checkpoint"):
                contracts.build_contracts(ROOT, LINUX, output)
            self.assertEqual(driver.call_args.kwargs, {"windows_user_link_aliases": True})
            self.assertFalse(output.exists())

    def test_publication_reuse_requires_the_selected_profile(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as temporary:
            output = Path(temporary) / "cupidc-contracts"
            output.mkdir()
            report = {"bootstrap": {"source_inputs": {"files": {SHIM: {}}}}}
            with mock.patch.object(contracts, "verify_publication", return_value=report), mock.patch.object(
                    contracts, "verify_publication_inputs"), mock.patch.object(contracts, "_require_report_manifest"), mock.patch.object(
                    contracts, "build_contracts", return_value={"rebuilt": True}) as build:
                self.assertIs(contracts.ensure_contracts(ROOT, LINUX, output, windows_user_link_aliases=True), report)
                build.assert_not_called()
                self.assertEqual(contracts.ensure_contracts(ROOT, LINUX, output, windows_user_link_aliases=False), {"rebuilt": True})
                self.assertEqual(build.call_args.kwargs, {"windows_user_link_aliases": False})

    def test_observer_captures_only_the_selected_producer_inventory(self):
        linux = bootstrap._candidate_build_plan(json.loads(LINUX.read_bytes())["build_plan"])
        with artifact_size_policy._PinnedRepository(ROOT) as reader:
            short = verifier._bootstrap_input_logical_paths(reader, linux)
            long = verifier._bootstrap_input_logical_paths(reader, linux, windows_user_link_aliases=True)
            self.assertEqual(len(short), 76)
            self.assertEqual(len(long), 77)
            self.assertNotIn(SHIM, short)
            self.assertEqual(set(long) - set(short), {SHIM})
            reader.require_unchanged()

    def test_current_cli_defaults_select_aliases_and_allow_historical_opt_out(self):
        for command, extra in (("bootstrap", []), ("bootstrap-windows", ["--plan-manifest", str(LINUX)])):
            args = [command, "--manifest", str(LINUX), "--root", str(ROOT), "--output", "unused", *extra]
            self.assertTrue(bootstrap._build_parser().parse_args(args).windows_user_link_aliases)
            self.assertFalse(bootstrap._build_parser().parse_args([*args, "--no-windows-user-link-aliases"]).windows_user_link_aliases)
            self.assertTrue(bootstrap._build_parser().parse_args([*args, "--windows-long-paths"]).windows_user_link_aliases)
            self.assertTrue(bootstrap._build_parser().parse_args([*args, "--windows-user-link-aliases"]).windows_user_link_aliases)
        for command in ("build", "ensure"):
            args = [command, "--manifest", str(LINUX), "--root", str(ROOT), "--output", "unused"]
            self.assertTrue(contracts._build_parser().parse_args(args).windows_user_link_aliases)
            self.assertFalse(contracts._build_parser().parse_args([*args, "--no-windows-user-link-aliases"]).windows_user_link_aliases)
            self.assertTrue(contracts._build_parser().parse_args([*args, "--windows-long-paths"]).windows_user_link_aliases)
            self.assertTrue(contracts._build_parser().parse_args([*args, "--windows-user-link-aliases"]).windows_user_link_aliases)

    def test_behavior_retarget_preserves_both_exact_profiles_and_parent_pair(self):
        native = bootstrap.verify_seed_inputs(WINDOWS)
        parent = bootstrap.verify_seed_inputs(LINUX)
        linux = bootstrap._candidate_build_plan(parent.manifest["build_plan"])
        for long_paths in (False, True):
            plan = bootstrap._windows_build_plan(linux, utf8=True, long_paths=long_paths, user_link_aliases=True)
            digest = bootstrap._build_plan_sha256(plan)
            snapshot = bootstrap.capture_source_snapshot(ROOT, linux, windows_utf8=True,
                windows_long_paths=long_paths, windows_user_link_aliases=True)
            changed = bootstrap._retarget_native_windows_behavior_seed(native, digest, linux, snapshot,
                utf8=True, long_paths=long_paths, user_link_aliases=True, parent_plan_seed=parent)
            provenance = changed.manifest["provenance"]
            self.assertEqual(provenance["source_input_count"], 78 if long_paths else 77)
            self.assertEqual(provenance["native_build_plan_sha256"], digest)
            self.assertEqual(provenance["parent_execution_seed_manifest_sha256"], native.manifest_sha256)
            self.assertEqual(provenance["parent_plan_seed_manifest_sha256"], parent.manifest_sha256)
            self.assertEqual(changed.artifact_bytes, native.artifact_bytes)
            with self.assertRaisesRegex(bootstrap.BootstrapError, "build plan differs"):
                bootstrap._retarget_native_windows_behavior_seed(native, digest, linux, snapshot,
                    utf8=True, long_paths=long_paths, parent_plan_seed=parent)

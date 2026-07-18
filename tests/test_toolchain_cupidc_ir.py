import os
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"


class ToolchainCupidCIRContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._build_directory = tempfile.TemporaryDirectory(
            prefix=".cupidc-ir-build-", dir=TOOLCHAIN_ROOT
        )
        build_path = Path(cls._build_directory.name)
        relative_build = build_path.relative_to(TOOLCHAIN_ROOT).as_posix()
        suffix = ".exe" if os.name == "nt" else ""
        cls.contract_path = build_path / ("cupidc-ir-contract" + suffix)
        target = f"{relative_build}/cupidc-ir-contract{suffix}"
        result = subprocess.run(
            [
                "make",
                "-C",
                str(TOOLCHAIN_ROOT),
                f"BUILD_DIR={relative_build}",
                target,
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        if result.returncode != 0 or not cls.contract_path.exists():
            cls._build_directory.cleanup()
            raise AssertionError(
                "CupidC IR contract build failed\n"
                + result.stdout
                + result.stderr
            )

    @classmethod
    def tearDownClass(cls):
        cls._build_directory.cleanup()

    def test_active_leaf_lowers_to_transactional_typed_ir(self):
        result = subprocess.run(
            [str(self.contract_path), "active-leaf", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "active-leaf: ok\n")

    def test_direct_forward_goto_lowers_to_function_relative_ir(self):
        result = subprocess.run(
            [str(self.contract_path), "forward-goto", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "forward-goto: ok\n")

    def test_goto_enters_nested_compound_at_the_label(self):
        result = subprocess.run(
            [str(self.contract_path), "nested-goto", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "nested-goto: ok\n")

    def test_switch_dispatches_once_to_resolved_case_targets(self):
        result = subprocess.run(
            [str(self.contract_path), "switch-lowering", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "switch-lowering: ok\n")

    def test_switch_and_loop_control_use_the_nearest_valid_target(self):
        result = subprocess.run(
            [str(self.contract_path), "switch-control", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "switch-control: ok\n")

    def test_nested_switches_keep_labels_and_reachability_isolated(self):
        result = subprocess.run(
            [str(self.contract_path), "switch-nesting", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "switch-nesting: ok\n")

    def test_integer_update_evaluates_its_destination_once(self):
        result = subprocess.run(
            [str(self.contract_path), "integer-updates", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "integer-updates: ok\n")

    def test_integer_compounds_evaluate_their_destination_once(self):
        result = subprocess.run(
            [str(self.contract_path), "integer-compounds", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "integer-compounds: ok\n")

    def test_integer_compounds_preserve_usual_arithmetic_conversions(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "integer-compound-conversions",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "integer-compound-conversions: ok\n")

    def test_integer_updates_preserve_promotions_and_qualifiers(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "integer-update-conversions",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "integer-update-conversions: ok\n")

    def test_integer_mutation_rejects_unsupported_targets_transactionally(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "integer-mutation-rejections",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "integer-mutation-rejections: ok\n")

    def test_object_pointer_parameters_reach_indirect_members(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "pointer-member-loads",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "pointer-member-loads: ok\n")

    def test_object_pointer_values_cross_supported_storage_and_calls(self):
        result = subprocess.run(
            [str(self.contract_path), "pointer-values", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "pointer-values: ok\n")

    def test_object_pointer_comparisons_preserve_c_pointer_semantics(self):
        result = subprocess.run(
            [str(self.contract_path), "pointer-comparisons", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "pointer-comparisons: ok\n")

    def test_object_pointer_conditions_use_scalar_truth_testing(self):
        result = subprocess.run(
            [str(self.contract_path), "pointer-conditions", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "pointer-conditions: ok\n")


if __name__ == "__main__":
    unittest.main()

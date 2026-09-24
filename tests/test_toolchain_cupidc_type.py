import os
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"


class ToolchainCupidCTypeLayoutContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._build_directory = tempfile.TemporaryDirectory(
            prefix=".cupidc-type-build-", dir=TOOLCHAIN_ROOT
        )
        build_path = Path(cls._build_directory.name)
        relative_build = build_path.relative_to(TOOLCHAIN_ROOT).as_posix()
        suffix = ".exe" if os.name == "nt" else ""
        cls.contract_path = build_path / ("cupidc-type-contract" + suffix)
        target = f"{relative_build}/cupidc-type-contract{suffix}"
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
                "CupidC type-layout contract build failed\n"
                + result.stdout
                + result.stderr
            )

    @classmethod
    def tearDownClass(cls):
        cls._build_directory.cleanup()

    def run_contract(self, mode):
        result = subprocess.run(
            [str(self.contract_path), mode],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, f"{mode}: ok\n")

    def test_i386_ilp32_types_have_explicit_target_layout_and_categories(self):
        self.run_contract("scalars")

    def test_manually_transcribed_active_fat16_abi_matches_wire_layout_oracles(self):
        self.run_contract("fat16")

    def test_records_unions_bit_fields_packing_and_alignment_compose(self):
        self.run_contract("records")

    def test_invalid_graphs_are_precise_transactional_and_recoverable(self):
        self.run_contract("errors")

    def test_large_graphs_are_immutable_deterministic_and_linear(self):
        self.run_contract("scale")

    def test_active_abi_shapes_and_repeated_results_survive_adversarial_graphs(self):
        self.run_contract("adversarial")


if __name__ == "__main__":
    unittest.main()

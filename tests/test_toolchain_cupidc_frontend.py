import os
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"


class ToolchainCupidCFrontendContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._build_directory = tempfile.TemporaryDirectory(
            prefix=".cupidc-frontend-build-", dir=TOOLCHAIN_ROOT
        )
        build_path = Path(cls._build_directory.name)
        relative_build = build_path.relative_to(TOOLCHAIN_ROOT).as_posix()
        suffix = ".exe" if os.name == "nt" else ""
        cls.contract_path = build_path / ("cupidc-frontend-contract" + suffix)
        target = f"{relative_build}/cupidc-frontend-contract{suffix}"
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
                "CupidC frontend contract build failed\n"
                + result.stdout
                + result.stderr
            )

    @classmethod
    def tearDownClass(cls):
        cls._build_directory.cleanup()

    def run_contract(self, mode):
        result = subprocess.run(
            [str(self.contract_path), mode, str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, f"{mode}: ok\n")

    def test_unchanged_fat16_closure_builds_typed_layouts(self):
        self.run_contract("fat16")

    def test_invalid_declarations_are_transactional_and_recoverable(self):
        self.run_contract("errors")

    def test_namespace_and_declarator_counts_have_no_private_frontend_caps(self):
        self.run_contract("scale")

    def test_c11_declaration_semantics_are_enforced_transactionally(self):
        self.run_contract("semantics")

    def test_i386_integer_constant_semantics_and_diagnostics(self):
        self.run_contract("constants")

    def test_public_boundaries_copy_ownership_and_fail_transactionally(self):
        self.run_contract("boundaries")

    def test_pathological_nesting_hits_a_transactional_limit(self):
        for mode in ("depth-declarator", "depth-constant", "depth-record"):
            with self.subTest(mode=mode):
                self.run_contract(mode)


if __name__ == "__main__":
    unittest.main()

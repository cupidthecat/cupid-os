import os
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"


class ToolchainX86ContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._build_directory = tempfile.TemporaryDirectory(
            prefix=".x86-build-", dir=TOOLCHAIN_ROOT
        )
        build_path = Path(cls._build_directory.name)
        relative_build = build_path.relative_to(TOOLCHAIN_ROOT)
        suffix = ".exe" if os.name == "nt" else ""
        cls.contract_path = build_path / ("x86-contract" + suffix)
        result = subprocess.run(
            [
                "make",
                "-C",
                str(TOOLCHAIN_ROOT),
                f"BUILD_DIR={relative_build}",
                "all",
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        if result.returncode != 0 or not cls.contract_path.exists():
            cls._build_directory.cleanup()
            raise AssertionError(
                "toolchain x86 contract build failed\n"
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

    def test_model_is_one_validated_deterministic_catalogue(self):
        self.run_contract("model")

    def test_model_inventory_and_fingerprint_are_locked(self):
        result = subprocess.run(
            [str(self.contract_path), "inventory"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "inventory: forms=587 mnemonics=242 registers=64 "
            "fingerprint=68E281CB\n",
        )

    def test_integer_encoding_decoding_and_relocation_fields(self):
        self.run_contract("integer")

    def test_conditional_moves_cover_modes_widths_sources_and_errors(self):
        self.run_contract("conditional-moves")

    def test_immediate_imul_selects_canonical_full_and_short_forms(self):
        self.run_contract("immediate-imul")

    def test_padding_nops_cover_compiler_forms_and_recovery(self):
        self.run_contract("padding-nops")

    def test_exact_clang_padding_nops_do_not_relax_prefix_rules(self):
        self.run_contract("clang-padding-nops")

    def test_mixed_16_32_bit_modrm_and_sib_addressing(self):
        self.run_contract("addressing")

    def test_relocations_and_requested_form_replay(self):
        self.run_contract("relocations")

    def test_system_x87_sse_and_sse2_forms_share_the_model(self):
        self.run_contract("system-simd")

    def test_active_source_families_decode_and_reencode_exactly(self):
        self.run_contract("active-surface")

    def test_invalid_and_malformed_inputs_are_distinguished(self):
        self.run_contract("errors")


if __name__ == "__main__":
    unittest.main()

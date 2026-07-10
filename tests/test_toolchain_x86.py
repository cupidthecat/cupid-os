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
            "inventory: forms=546 mnemonics=226 registers=64 "
            "fingerprint=3159218E\n",
        )

    def test_integer_encoding_decoding_and_relocation_fields(self):
        self.run_contract("integer")

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

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"


class ToolchainCupidCPreprocessorContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._build_directory = tempfile.TemporaryDirectory(
            prefix=".cupidc-pp-build-", dir=TOOLCHAIN_ROOT
        )
        build_path = Path(cls._build_directory.name)
        relative_build = build_path.relative_to(TOOLCHAIN_ROOT).as_posix()
        suffix = ".exe" if os.name == "nt" else ""
        cls.contract_path = build_path / ("cupidc-pp-contract" + suffix)
        target = f"{relative_build}/cupidc-pp-contract{suffix}"
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
                "CupidC preprocessor contract build failed\n"
                + result.stdout
                + result.stderr
            )

    @classmethod
    def tearDownClass(cls):
        cls._build_directory.cleanup()

    def run_contract(self, mode, *arguments):
        result = subprocess.run(
            [str(self.contract_path), mode, *(str(value) for value in arguments)],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, f"{mode}: ok\n")

    def test_translation_phases_preserve_tokens_and_locations(self):
        self.run_contract("phases")

    def test_lexical_errors_roll_back_the_operation(self):
        self.run_contract("errors")

    def test_token_spellings_are_owned_and_use_longest_match(self):
        self.run_contract("tokens")

    def test_unimplemented_configuration_fails_explicitly(self):
        self.run_contract("unsupported")

    def test_full_c11_conditional_expressions_follow_integer_semantics(self):
        self.run_contract("conditional-expressions")

    def test_language_predefined_macros_are_target_deterministic(self):
        self.run_contract("predefined")

    def test_predefined_paths_follow_forced_included_and_invocation_sources(self):
        self.run_contract("predefined-files")

    def test_predefined_ownership_failures_are_transactional(self):
        self.run_contract("predefined-errors")

    def test_once_and_pack_pragmas_update_source_and_tape_state(self):
        self.run_contract("pragmas")

    def test_once_and_pack_state_cross_include_boundaries(self):
        self.run_contract("pragma-files")

    def test_pragma_failures_are_precise_and_transactional(self):
        self.run_contract("pragma-errors")

    def test_pack_stack_depth_and_reuse_are_bounded(self):
        self.run_contract("pragma-scale")

    def test_conditional_expression_failures_are_precise_and_transactional(self):
        self.run_contract("conditional-errors")

    def test_every_active_conditional_shape_accepts_target_profile_actions(self):
        self.run_contract("conditional-active")

    def test_conditional_expression_work_and_nesting_are_bounded(self):
        self.run_contract("conditional-scale")

    def test_function_macros_expand_with_standard_argument_rescanning(self):
        self.run_contract("function-macros")

    def test_function_macro_substitution_scales_at_the_parameter_limit(self):
        self.run_contract("function-scale")

    def test_variadic_stringify_and_paste_macros_follow_c11_and_gnu_rules(self):
        self.run_contract("macro-operators")

    def test_gnu_variadic_omission_is_distinct_from_an_empty_argument(self):
        self.run_contract("macro-gnu")

    def test_stringify_paste_and_variadic_work_are_bounded_at_scale(self):
        self.run_contract("macro-operator-scale")

    def test_active_x86_variadic_call_manifest_is_accepted_from_any_cwd(self):
        self.run_contract("macro-active-cases", TOOLCHAIN_ROOT)

    def test_macro_operator_failures_are_useful_and_transactional(self):
        self.run_contract("macro-operator-errors")

    def test_function_macro_failures_are_useful_and_transactional(self):
        self.run_contract("function-errors")

    def test_object_macros_conditionals_and_includes_share_one_state(self):
        self.run_contract("object-includes")

    def test_directive_failures_are_useful_and_transactional(self):
        self.run_contract("directive-errors")

    def test_configured_request_limits_roll_back_and_recover(self):
        self.run_contract("limits")


if __name__ == "__main__":
    unittest.main()

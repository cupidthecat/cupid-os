import os
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"


class ToolchainCupidCObjectContractTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._build_directory = tempfile.TemporaryDirectory(
            prefix=".cupidc-object-build-", dir=TOOLCHAIN_ROOT
        )
        build_path = Path(cls._build_directory.name)
        relative_build = build_path.relative_to(TOOLCHAIN_ROOT).as_posix()
        suffix = ".exe" if os.name == "nt" else ""
        cls.contract_path = build_path / ("cupidc-object-contract" + suffix)
        target = f"{relative_build}/cupidc-object-contract{suffix}"
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
                "CupidC object contract build failed\n"
                + result.stdout
                + result.stderr
            )

    @classmethod
    def tearDownClass(cls):
        cls._build_directory.cleanup()

    def test_static_definitions_emit_deterministic_elf32_objects(self):
        result = subprocess.run(
            [str(self.contract_path), "static-data", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "static-data: ok\n")

    def test_direct_goto_emits_resolved_intra_function_branches(self):
        result = subprocess.run(
            [str(self.contract_path), "direct-goto", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "direct-goto: ok\n")

    def test_switch_emits_resolved_dispatch_branches(self):
        result = subprocess.run(
            [str(self.contract_path), "switch-object", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "switch-object: ok\n")

    def test_integer_mutation_emits_deterministic_i386_objects(self):
        result = subprocess.run(
            [str(self.contract_path), "integer-mutation", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "integer-mutation: ok\n")

    def test_object_pointer_values_emit_deterministic_i386_objects(self):
        result = subprocess.run(
            [str(self.contract_path), "pointer-values", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "pointer-values: ok\n")

    def test_object_pointer_comparisons_emit_unsigned_i386_predicates(self):
        result = subprocess.run(
            [str(self.contract_path), "pointer-comparisons", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "pointer-comparisons: ok\n")

    def test_object_pointer_conditions_emit_zero_tests_and_branches(self):
        result = subprocess.run(
            [str(self.contract_path), "pointer-conditions", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "pointer-conditions: ok\n")

    def test_object_pointer_arithmetic_emits_scaled_i386_operations(self):
        result = subprocess.run(
            [str(self.contract_path), "pointer-arithmetic", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "pointer-arithmetic: ok\n")

    def test_function_pointer_calls_emit_deterministic_i386_objects(self):
        result = subprocess.run(
            [str(self.contract_path), "function-pointers", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "function-pointers: ok\n")

    def test_fixed_automatic_objects_use_target_sized_i386_frames(self):
        result = subprocess.run(
            [str(self.contract_path), "automatic-objects", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "automatic-objects: ok\n")

    def test_block_extern_objects_emit_one_undefined_linked_symbol(self):
        result = subprocess.run(
            [str(self.contract_path), "block-externs", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "block-externs: ok\n")

    def test_block_function_declarations_match_file_scope_prototypes(self):
        result = subprocess.run(
            [str(self.contract_path), "block-functions", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "block-functions: ok\n")

    def test_block_enumerators_emit_like_direct_integer_constants(self):
        result = subprocess.run(
            [str(self.contract_path), "block-enums", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "block-enums: ok\n")

    def test_bit_field_assignments_emit_value_preserving_stores(self):
        result = subprocess.run(
            [str(self.contract_path), "bit-field-stores", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "bit-field-stores: ok\n")

    def test_bit_field_mutations_execute_with_single_designator_evaluation(self):
        result = subprocess.run(
            [str(self.contract_path), "bit-field-mutations", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "bit-field-mutations: ok\n")

    def test_block_typedefs_do_not_change_emitted_objects(self):
        result = subprocess.run(
            [str(self.contract_path), "block-typedefs", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "block-typedefs: ok\n")

    def test_automatic_aggregate_initializers_zero_then_store_subobjects(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "aggregate-initializers",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "aggregate-initializers: ok\n")

    def test_narrow_integer_mutation_emits_exact_width_i386_stores(self):
        result = subprocess.run(
            [str(self.contract_path), "narrow-mutations", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "narrow-mutations: ok\n")

    def test_narrow_integer_values_use_width_correct_i386_operations(self):
        result = subprocess.run(
            [str(self.contract_path), "narrow-values", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "narrow-values: ok\n")

    def test_explicit_void_casts_emit_exact_discard_code(self):
        result = subprocess.run(
            [str(self.contract_path), "void-casts", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "void-casts: ok\n")

    def test_structure_values_follow_the_i386_cdecl_memory_abi(self):
        result = subprocess.run(
            [str(self.contract_path), "structure-values", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "structure-values: ok\n")

    def test_calls_align_the_i386_stack_to_sixteen_bytes(self):
        result = subprocess.run(
            [str(self.contract_path), "call-alignment", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "call-alignment: ok\n")

    def test_block_static_objects_use_static_elf_storage(self):
        result = subprocess.run(
            [str(self.contract_path), "block-statics", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "block-statics: ok\n")

    def test_compound_literals_initialize_storage_before_structure_calls(self):
        result = subprocess.run(
            [str(self.contract_path), "compound-literals", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "compound-literals: ok\n")

    def test_variadic_callees_follow_the_i386_cursor_abi(self):
        result = subprocess.run(
            [str(self.contract_path), "variadic-callees", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "variadic-callees: ok\n")

    def test_wide_variadics_follow_the_i386_cursor_abi(self):
        result = subprocess.run(
            [str(self.contract_path), "wide-variadics", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "wide-variadics: ok\n")

    def test_floating_transport_models_selected_ieee_patterns(self):
        result = subprocess.run(
            [str(self.contract_path), "floating-transport", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "floating-transport: ok\n")

    def test_floating_arithmetic_spills_each_x87_result(self):
        result = subprocess.run(
            [str(self.contract_path), "floating-arithmetic", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "floating-arithmetic: ok\n")

    def test_old_style_empty_functions_emit_deterministic_i386_objects(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "old-style-empty-functions",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "old-style-empty-functions: ok\n")

    def test_block_record_tags_emit_deterministic_i386_objects(self):
        result = subprocess.run(
            [str(self.contract_path), "block-records", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "block-records: ok\n")

    def test_wide_shifts_bitwise_operations_and_conversions_execute(self):
        result = subprocess.run(
            [str(self.contract_path), "wide-returns", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "wide-returns: ok\n")

    def test_wide_comparisons_and_conditions_execute(self):
        result = subprocess.run(
            [str(self.contract_path), "wide-conditions", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "wide-conditions: ok\n")

    def test_wide_integer_mutations_preserve_snapshots_and_cdecl_state(self):
        result = subprocess.run(
            [str(self.contract_path), "wide-mutations", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "wide-mutations: ok\n")

    def test_self_host_frontier_values_emit_execute_and_recover_together(self):
        result = subprocess.run(
            [str(self.contract_path), "self-host-frontier", str(REPO_ROOT)],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "self-host-frontier: ok\n")

    def test_i386_linux_host_adapters_emit_deterministic_objects(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "self-host-hosted-adapters",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "self-host-hosted-adapters: ok\n")

    def test_i386_linux_host_profile_rejects_missing_or_wrong_abi(self):
        result = subprocess.run(
            [
                str(self.contract_path),
                "self-host-hosted-profile-errors",
                str(REPO_ROOT),
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout, "self-host-hosted-profile-errors: ok\n"
        )


if __name__ == "__main__":
    unittest.main()

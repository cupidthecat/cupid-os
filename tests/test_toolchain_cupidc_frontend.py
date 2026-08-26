import json
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

    def test_unchanged_kernel_header_merges_compatible_redeclarations(self):
        self.run_contract("redeclarations")

    def test_gnu_attributes_preserve_declaration_and_layout_semantics(self):
        self.run_contract("attributes")

    def test_gnu_weak_attributes_mark_linked_entities(self):
        self.run_contract("weak-attributes")

    def test_gnu_section_attributes_publish_owned_entity_metadata(self):
        self.run_contract("section-attributes")

    def test_gnu_unused_attributes_mark_canonical_entities(self):
        self.run_contract("unused-attributes")

    def test_gnu_used_attributes_retain_file_scope_definitions(self):
        self.run_contract("used-attributes")

    def test_gnu_function_codegen_attributes_merge_on_canonical_functions(self):
        self.run_contract("function-codegen-attributes")

    def test_naked_functions_publish_typed_ipi_wrapper_metadata(self):
        self.run_contract("naked-functions")

    def test_static_asserts_use_target_sizeof_and_integer_relations(self):
        self.run_contract("static-asserts")

    def test_function_bodies_publish_typed_call_ast(self):
        self.run_contract("function-bodies")

    def test_cupid_builtin_types_parse_without_changing_c11_identifiers(self):
        self.run_contract("cupid-types")

    def test_old_style_empty_function_definitions_publish_typed_bodies(self):
        self.run_contract("old-style-empty-functions")

    def test_doom_implicit_calls_keep_old_style_function_identity(self):
        self.run_contract("doom-implicit-functions")

    def test_variadic_callees_publish_cursor_builtins(self):
        self.run_contract("variadic-callees")

    def test_wide_variadics_preserve_post_conversion_argument_types(self):
        self.run_contract("wide-variadics")

    def test_floating_values_preserve_exact_types_during_transport(self):
        self.run_contract("floating-transport")

    def test_same_kind_floating_values_support_arithmetic(self):
        self.run_contract("floating-arithmetic")

    def test_floating_comparisons_follow_c11_ordering_rules(self):
        self.run_contract("floating-comparisons")

    def test_floating_width_conversions_follow_c11_value_rules(self):
        self.run_contract("floating-conversions")

    def test_wide_integers_convert_to_float_and_double_at_runtime(self):
        self.run_contract("wide-integer-floating")

    def test_floating_truth_and_boolean_conversions_follow_c11(self):
        self.run_contract("floating-truth")

    def test_static_floating_arithmetic_is_folded_without_host_fp(self):
        self.run_contract("floating-scalars")

    def test_static_long_double_arithmetic_is_exact_and_target_only(self):
        self.run_contract("static-long-double-arithmetic")

    def test_atomic_builtins_publish_typed_checked_expressions(self):
        self.run_contract("atomic-builtins")

    def test_complete_inline_assembly_contract_is_executed(self):
        self.run_contract("inline-assembly")

    def test_pointer_output_inline_assembly_preserves_pointer_types(self):
        self.run_contract("pointer-output-assembly")

    def test_register_snapshot_assembly_preserves_exact_public_metadata(self):
        self.run_contract("register-snapshot-assembly")

    def test_call_next_assembly_preserves_exact_public_metadata(self):
        self.run_contract("call-next-assembly")

    def test_port_io_assembly_keeps_widths_registers_and_memory_clobber(self):
        self.run_contract("port-io-assembly")

    def test_privileged_register_assembly_preserves_inputs_and_outputs(self):
        self.run_contract("privileged-register-assembly")

    def test_fxsave_assembly_keeps_its_independent_pointer_input(self):
        self.run_contract("fxsave-assembly")

    def test_ldmxcsr_assembly_keeps_its_independent_memory_input(self):
        self.run_contract("ldmxcsr-memory-input")

    def test_movss_assembly_keeps_float_memory_operands_and_xmm0_clobber(
        self,
    ):
        self.run_contract("movss-memory-assembly")

    def test_kernel_simd_assembly_keeps_pointer_inputs_and_xmm_clobbers(self):
        self.run_contract("kernel-simd-assembly")

    def test_x87_sine_assembly_keeps_double_memory_operands(self):
        self.run_contract("x87-sine-memory-assembly")

    def test_x87_round_down_assembly_keeps_control_and_memory_metadata(self):
        self.run_contract("x87-round-down-memory-assembly")

    def test_x87_pow_assembly_keeps_all_five_double_memory_operands(self):
        self.run_contract("x87-pow-memory-assembly")

    def test_x87_powf_assembly_keeps_mixed_width_memory_operands(self):
        self.run_contract("x87-powf-memory-assembly")

    def test_sqrtsd_assembly_keeps_double_xmm_operands(self):
        self.run_contract("sqrtsd-register-assembly")

    def test_x87_atan2_assembly_keeps_named_double_memory_operands(self):
        self.run_contract("x87-atan2-memory-assembly")

    def test_x87_exp_assembly_keeps_named_double_memory_operands(self):
        self.run_contract("x87-exp-memory-assembly")

    def test_descriptor_table_assembly_keeps_exact_segment_metadata(self):
        self.run_contract("descriptor-table-assembly")

    def test_legacy_port_constraints_keep_the_dx_fallback(self):
        self.run_contract("legacy-port-assembly")

    def test_machine_state_memory_outputs_retain_exact_widths(self):
        self.run_contract("state-memory-assembly")

    def test_inline_assembly_preserves_flags_restore_cc_clobber(self):
        self.run_contract("inline-assembly")

    def test_operand_free_and_empty_barrier_assembly_is_represented(self):
        self.run_contract("operand-free-assembly")

    def test_kernel_start_assembly_keeps_exact_clobber_metadata(self):
        self.run_contract("kernel-start-assembly")

    def test_file_scope_basic_assembly_has_an_independent_public_table(self):
        self.run_contract("file-scope-assembly")

    def test_fabs_mask_and_wrappers_keep_source_order_and_prototypes(self):
        self.run_contract("file-scope-fabs-assembly")

    def test_block_declarations_publish_typed_lexical_bindings(self):
        self.run_contract("block-bindings")

    def test_block_function_declarations_keep_linked_identity(self):
        self.run_contract("block-functions")

    def test_block_typedefs_publish_typed_lexical_aliases(self):
        self.run_contract("block-typedefs")

    def test_block_extern_objects_keep_linked_identity(self):
        self.run_contract("block-externs")

    def test_block_enum_declarations_publish_lexical_constants(self):
        self.run_contract("block-enums")

    def test_block_record_definitions_keep_lexical_type_identity(self):
        self.run_contract("block-records")

    def test_scalar_automatic_initializers_are_typed_in_source_order(self):
        self.run_contract("scalar-initializers")

    def test_static_local_initializers_are_typed_as_static_data(self):
        self.run_contract("static-initializers")

    def test_static_aggregate_initializers_retain_subobject_values(self):
        self.run_contract("aggregate-initializers")

    def test_automatic_aggregate_initializers_retain_runtime_subobjects(self):
        self.run_contract("automatic-aggregate-initializers")

    def test_union_initializers_select_one_active_member(self):
        self.run_contract("union-initializers")

    def test_designated_initializers_select_direct_subobjects(self):
        self.run_contract("designated-initializers")

    def test_file_scope_initializers_publish_object_definitions(self):
        self.run_contract("file-scope-initializers")

    def test_scalar_operators_assignments_and_returns_are_typed(self):
        self.run_contract("scalar-returns")

    def test_conditional_expressions_publish_typed_branching_values(self):
        self.run_contract("conditional-expressions")

    def test_aggregate_values_flow_through_typed_expressions(self):
        self.run_contract("aggregate-values")

    def test_block_scope_compound_literals_publish_unnamed_objects(self):
        self.run_contract("compound-literals")

    def test_for_statements_publish_typed_counted_control_flow(self):
        self.run_contract("for-statements")

    def test_comma_expressions_preserve_sequence_and_grammar_boundaries(self):
        self.run_contract("comma-expressions")

    def test_if_statements_publish_typed_selection_control_flow(self):
        self.run_contract("if-statements")

    def test_while_statements_publish_typed_iteration_control_flow(self):
        self.run_contract("while-statements")

    def test_do_statements_publish_typed_iteration_control_flow(self):
        self.run_contract("do-statements")

    def test_switch_statements_publish_typed_selection_control_flow(self):
        self.run_contract("switch-statements")

    def test_labels_and_goto_publish_function_scoped_control_flow(self):
        self.run_contract("labels-and-goto")

    def test_lvalue_designators_and_layout_queries_are_typed(self):
        self.run_contract("pointer-expressions")

    def test_function_pointer_casts_retain_exact_source_and_target_types(self):
        self.run_contract("function-pointer-casts")

    def test_doom_pointer_compatibility_is_explicit_typed_and_source_driven(self):
        self.run_contract("doom-compatibility-pointers")

    def test_pointer_arithmetic_and_subscripts_are_typed(self):
        self.run_contract("pointer-arithmetic")

    def test_pointer_comparisons_are_typed(self):
        self.run_contract("pointer-comparisons")

    def test_compound_assignments_and_updates_evaluate_lvalues_once(self):
        self.run_contract("scalar-updates")

    def test_inline_function_specifiers_are_retained_semantically(self):
        self.run_contract("function-specifiers")

    def test_active_inline_inventory_is_drift_gated(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        feature = next(
            item for item in audit["features"] if item["id"] == "c.storage.inline"
        )
        self.assertEqual(feature["occurrences"], 131)
        self.assertEqual(
            feature["files"],
            [
                "bin/feature24_widetypes.cc",
                "drivers/timer.cc",
                "drivers/vga.cc",
                "kernel/audio/nuked_opl3.cc",
                "kernel/core/debug.h",
                "kernel/core/kernel.cc",
                "kernel/core/ports.h",
                "kernel/cpu/cpu.h",
                "kernel/cpu/fpu.cc",
                "kernel/cpu/irq.cc",
                "kernel/cpu/pic.cc",
                "kernel/doom/doomgeneric_cupidos.cc",
                "kernel/doom/src/i_scale.cc",
                "kernel/doom/src/i_swap.h",
                "kernel/gfx/gfx2d_handoff.h",
                "kernel/gfx/glyph_raster.cc",
                "kernel/mm/memory.cc",
                "kernel/smp/percpu.h",
                "kernel/usb/ehci.cc",
                "kernel/usb/uhci.cc",
                "kernel/usb/usb_hc.h",
                "user/cupid.h",
            ],
        )

    def test_active_static_assert_inventory_is_drift_gated(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        feature = next(
            item
            for item in audit["features"]
            if item["id"] == "c.declaration.static_assert"
        )
        self.assertEqual(feature["occurrences"], 28)
        self.assertEqual(
            feature["files"],
            [
                "kernel/core/process.cc",
                "kernel/core/syscall.cc",
                "kernel/lang/exec.cc",
                "kernel/mm/memory.h",
                "kernel/smp/percpu.h",
            ],
        )

    def test_active_return_inventory_is_drift_gated(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        feature = next(
            item for item in audit["features"] if item["id"] == "c.control.return"
        )
        self.assertEqual(feature["occurrences"], 26024)

    def test_active_for_statement_inventory_is_drift_gated(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        feature = next(
            item for item in audit["features"] if item["id"] == "c.control.for"
        )
        self.assertEqual(feature["occurrences"], 4724)

    def test_active_while_statement_inventory_is_drift_gated(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        feature = next(
            item for item in audit["features"] if item["id"] == "c.control.while"
        )
        self.assertEqual(feature["occurrences"], 2918)
        self.assertEqual(len(feature["files"]), 271)

    def test_active_do_statement_inventory_is_drift_gated(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        feature = next(
            item for item in audit["features"] if item["id"] == "c.control.do"
        )
        self.assertEqual(feature["occurrences"], 90)
        self.assertEqual(len(feature["files"]), 49)

    def test_active_switch_label_inventory_is_drift_gated(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        features = {item["id"]: item for item in audit["features"]}
        self.assertEqual(features["c.control.switch"]["occurrences"], 236)
        self.assertEqual(len(features["c.control.switch"]["files"]), 72)
        self.assertEqual(features["c.control.case"]["occurrences"], 1777)
        self.assertEqual(len(features["c.control.case"]["files"]), 72)
        self.assertEqual(features["c.control.default"]["occurrences"], 166)
        self.assertEqual(len(features["c.control.default"]["files"]), 59)

    def test_active_if_else_inventory_is_drift_gated(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        features = {item["id"]: item for item in audit["features"]}
        self.assertEqual(features["c.control.if"]["occurrences"], 42016)
        self.assertEqual(len(features["c.control.if"]["files"]), 382)
        self.assertEqual(features["c.control.else"]["occurrences"], 5257)
        self.assertEqual(len(features["c.control.else"]["files"]), 287)

    def test_active_goto_inventory_is_drift_gated(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        feature = next(
            item for item in audit["features"] if item["id"] == "c.control.goto"
        )
        self.assertEqual(feature["occurrences"], 3228)
        self.assertEqual(len(feature["files"]), 30)

    def test_active_non_doom_header_frontier_is_drift_gated(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        excluded = {
            "bin/fat16.h",
            "bin/shell.h",
            "user/cupid.h",
            "toolchain/hosted/i386-linux/include/cupid_host_abi.h",
            "toolchain/hosted/i386-linux/include/direct.h",
            "toolchain/hosted/i386-linux/include/errno.h",
            "toolchain/hosted/i386-linux/include/stdint.h",
            "toolchain/hosted/i386-linux/include/stdio.h",
            "toolchain/hosted/i386-linux/include/stdlib.h",
            "toolchain/hosted/i386-linux/include/string.h",
            "toolchain/hosted/i386-linux/include/unistd.h",
            "toolchain/hosted/i386-linux/include/windows.h",
            "toolchain/cupidbuild_host.h",
        }
        headers = sorted(
            "/" + source["path"]
            for source in audit["sources"]
            if source["language"] == "c_header"
            and not source["path"].startswith("kernel/doom/")
            and source["path"] not in excluded
        )
        failures = {
            "/kernel/core/scheduler.h": (
                "/kernel/core/scheduler.h",
                16,
                37,
                "0x0b000007",
            ),
            "/kernel/cpu/simd_intrin.h": (
                "/kernel/cpu/simd_intrin.h",
                28,
                1,
                "0x0b000003",
            ),
            "/toolchain/tests/cupidc_exact_floating_literal_fixture.h": (
                "/toolchain/tests/cupidc_exact_floating_literal_fixture.h",
                71,
                1,
                "0x0b000003",
            ),
        }
        self.assertEqual(len(headers), 167)
        self.assertEqual(len(failures), 3)
        expected_lines = []
        for header in headers:
            if header not in failures:
                expected_lines.append(f"PASS\t{header}")
                continue
            path, line, column, code = failures[header]
            expected_lines.append(
                f"FAIL\t{header}\tinput\t{code}\t{path}\t{line}\t{column}"
            )
        expected_lines.append("header-sweep: ok 164 3")
        result = subprocess.run(
            [
                str(self.contract_path),
                "header-sweep",
                str(REPO_ROOT),
                *headers,
            ],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertEqual(result.stdout, "\n".join(expected_lines) + "\n")

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

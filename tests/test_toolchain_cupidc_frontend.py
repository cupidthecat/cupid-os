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

    def test_static_asserts_use_target_sizeof_and_integer_relations(self):
        self.run_contract("static-asserts")

    def test_function_bodies_publish_typed_call_ast(self):
        self.run_contract("function-bodies")

    def test_old_style_empty_function_definitions_publish_typed_bodies(self):
        self.run_contract("old-style-empty-functions")

    def test_variadic_callees_publish_cursor_builtins(self):
        self.run_contract("variadic-callees")

    def test_block_declarations_publish_typed_lexical_bindings(self):
        self.run_contract("block-bindings")

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
        self.assertEqual(feature["occurrences"], 107)
        self.assertEqual(
            feature["files"],
            [
                "bin/feature24_widetypes.cc",
                "drivers/timer.c",
                "drivers/vga.c",
                "kernel/audio/nuked_opl3.c",
                "kernel/core/debug.h",
                "kernel/core/kernel.c",
                "kernel/core/ports.h",
                "kernel/cpu/cpu.h",
                "kernel/cpu/fpu.c",
                "kernel/cpu/irq.c",
                "kernel/cpu/pic.c",
                "kernel/doom/doomgeneric_cupidos.c",
                "kernel/doom/src/i_scale.c",
                "kernel/doom/src/i_swap.h",
                "kernel/gfx/glyph_raster.c",
                "kernel/mm/memory.c",
                "kernel/smp/percpu.h",
                "kernel/usb/ehci.c",
                "kernel/usb/uhci.c",
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
        self.assertEqual(feature["occurrences"], 22)
        self.assertEqual(
            feature["files"],
            [
                "kernel/core/process.c",
                "kernel/core/syscall.c",
                "kernel/lang/exec.c",
                "kernel/smp/percpu.h",
            ],
        )

    def test_active_return_inventory_is_drift_gated(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        feature = next(
            item for item in audit["features"] if item["id"] == "c.control.return"
        )
        self.assertEqual(feature["occurrences"], 16402)

    def test_active_for_statement_inventory_is_drift_gated(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        feature = next(
            item for item in audit["features"] if item["id"] == "c.control.for"
        )
        self.assertEqual(feature["occurrences"], 3170)

    def test_active_while_statement_inventory_is_drift_gated(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        feature = next(
            item for item in audit["features"] if item["id"] == "c.control.while"
        )
        self.assertEqual(feature["occurrences"], 2521)
        self.assertEqual(len(feature["files"]), 253)

    def test_active_do_statement_inventory_is_drift_gated(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        feature = next(
            item for item in audit["features"] if item["id"] == "c.control.do"
        )
        self.assertEqual(feature["occurrences"], 62)
        self.assertEqual(len(feature["files"]), 37)

    def test_active_switch_label_inventory_is_drift_gated(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        features = {item["id"]: item for item in audit["features"]}
        self.assertEqual(features["c.control.switch"]["occurrences"], 206)
        self.assertEqual(len(features["c.control.switch"]["files"]), 67)
        self.assertEqual(features["c.control.case"]["occurrences"], 1586)
        self.assertEqual(len(features["c.control.case"]["files"]), 67)
        self.assertEqual(features["c.control.default"]["occurrences"], 137)
        self.assertEqual(len(features["c.control.default"]["files"]), 54)

    def test_active_if_else_inventory_is_drift_gated(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        features = {item["id"]: item for item in audit["features"]}
        self.assertEqual(features["c.control.if"]["occurrences"], 26814)
        self.assertEqual(len(features["c.control.if"]["files"]), 361)
        self.assertEqual(features["c.control.else"]["occurrences"], 3659)
        self.assertEqual(len(features["c.control.else"]["files"]), 273)

    def test_active_goto_inventory_is_drift_gated(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        feature = next(
            item for item in audit["features"] if item["id"] == "c.control.goto"
        )
        self.assertEqual(feature["occurrences"], 1177)
        self.assertEqual(len(feature["files"]), 24)

    def test_active_non_doom_header_frontier_is_drift_gated(self):
        audit_path = REPO_ROOT / "docs/bootstrap/audits/active-build.json"
        audit = json.loads(audit_path.read_text(encoding="utf-8"))
        excluded = {"bin/fat16.h", "bin/shell.h", "user/cupid.h"}
        headers = sorted(
            "/" + source["path"]
            for source in audit["sources"]
            if source["language"] == "c_header"
            and not source["path"].startswith("kernel/doom/")
            and source["path"] not in excluded
        )
        failures = {
            "/kernel/smp/mp_tables.h": (
                "/kernel/smp/percpu.h", 44, 5, "0x0b00000f"
            ),
            "/kernel/smp/percpu.h": (
                "/kernel/smp/percpu.h", 44, 5, "0x0b00000f"
            ),
            "/kernel/smp/smp.h": (
                "/kernel/smp/percpu.h", 44, 5, "0x0b00000f"
            ),
            "/kernel/core/ports.h": (
                "/kernel/core/ports.h", 8, 5, "0x0b00000f"
            ),
            "/kernel/cpu/cpu.h": (
                "/kernel/cpu/cpu.h", 23, 5, "0x0b00000f"
            ),
        }
        self.assertEqual(len(headers), 154)
        self.assertEqual(len(failures), 5)
        expected_lines = []
        for header in headers:
            if header not in failures:
                expected_lines.append(f"PASS\t{header}")
                continue
            path, line, column, code = failures[header]
            expected_lines.append(
                f"FAIL\t{header}\tunsupported\t{code}\t{path}\t{line}\t{column}"
            )
        expected_lines.append("header-sweep: ok 149 5")
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

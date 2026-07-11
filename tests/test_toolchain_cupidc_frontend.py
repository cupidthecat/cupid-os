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
                "/kernel/smp/percpu.h", 42, 41, "0x0b000008"
            ),
            "/kernel/smp/percpu.h": (
                "/kernel/smp/percpu.h", 42, 41, "0x0b000008"
            ),
            "/kernel/smp/smp.h": (
                "/kernel/smp/percpu.h", 42, 41, "0x0b000008"
            ),
            "/kernel/core/debug.h": (
                "/kernel/core/debug.h", 8, 71, "0x0b000008"
            ),
            "/kernel/core/ports.h": (
                "/kernel/core/ports.h", 6, 42, "0x0b000008"
            ),
            "/kernel/cpu/cpu.h": (
                "/kernel/cpu/cpu.h", 21, 36, "0x0b000008"
            ),
        }
        self.assertEqual(len(headers), 152)
        self.assertEqual(len(failures), 6)
        expected_lines = []
        for header in headers:
            if header not in failures:
                expected_lines.append(f"PASS\t{header}")
                continue
            path, line, column, code = failures[header]
            expected_lines.append(
                f"FAIL\t{header}\tunsupported\t{code}\t{path}\t{line}\t{column}"
            )
        expected_lines.append("header-sweep: ok 146 6")
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

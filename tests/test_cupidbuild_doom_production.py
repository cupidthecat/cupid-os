import copy
import subprocess
import unittest
from pathlib import Path

from tools import build_graph_audit as audit
from tools.cupidc_kernel_compile import APPROVED_DOOM_COMPAT_SOURCES, APPROVED_DOOM_TREE_SOURCES


ROOT = Path(__file__).resolve().parents[1]
SOURCES = (*APPROVED_DOOM_COMPAT_SOURCES, *APPROVED_DOOM_TREE_SOURCES)


class CupidBuildDoomProductionTests(unittest.TestCase):
    def test_native_cohort_matches_approved_profiles(self):
        self.assertEqual(audit._CUPIDBUILD_DOOM_COMPAT_SOURCES, APPROVED_DOOM_COMPAT_SOURCES)
        self.assertEqual(audit._CUPIDBUILD_DOOM_TREE_SOURCES, APPROVED_DOOM_TREE_SOURCES)
        self.assertEqual(len(SOURCES), 83)
        self.assertEqual(len(set(SOURCES)), 83)

    def test_delivery_contract_rejects_recipe_and_input_changes(self):
        seed = ["seed/manifest.json"] + [f"seed/{tool}.elf" for tool in
            ("cupidc", "cupidasm", "cupiddis", "cupidld", "cupidobj", "cupidbuild")]
        headers = ["kernel/doom/dglibc_compat.h", "kernel/doom/src/include_stubs/sys/types.h"]
        for source in SOURCES:
            output = Path(source).with_suffix(".o").as_posix()
            transform = {
                "output": output, "operation": "compile_c_to_elf32_object",
                "tools": ["cupid_builder", "cupid_c_compiler"],
                "inputs": [source, *headers, "Makefile", *seed,
                           "build/bootstrap/doom-cupidc-inputs.json"],
                "recipe": audit._cupidbuild_doom_compile_recipe(source),
            }
            def check(value):
                audit._validate_cupidbuild_doom_compile_delivery(
                    [value], seed_inputs=seed, headers=headers)
            with self.subTest(source=source):
                check(transform)
                self.assertEqual(audit._c_preprocessor_profile_for_c_transform(".", transform),
                    "DOOM_COMPAT_I386" if source in APPROVED_DOOM_COMPAT_SOURCES else "DOOM_TREE_I386")
                for removed in transform["inputs"]:
                    value = copy.deepcopy(transform)
                    value["inputs"].remove(removed)
                    with self.subTest(removed=removed), self.assertRaises(audit.AuditError):
                        check(value)
                for mutation in (
                    {"tools": ["cupid_c_compiler", "host_python"]},
                    {"operation": "compile_c_to_host_object"},
                    {"inputs": transform["inputs"] + ["tools/cupidc_kernel_compile.py"]},
                    {"inputs": transform["inputs"] + [source]},
                    {"order_only_inputs": ["build/bootstrap/doom-cupidc-inputs.json"]},
                    {"recipe": transform["recipe"] + ["echo unchecked"]},
                    {"recipe": transform["recipe"][:-1] + ["--source other.cc --output other.o"]},
                    {"recipe": transform["recipe"][:-1] + [transform["recipe"][-1] + " --timeout 1"]},
                    {"recipe": transform["recipe"][:-1] + [transform["recipe"][-1] + " --profile doom-tree"]},
                    {"recipe": transform["recipe"][:-1] + [transform["recipe"][-1] + " -DDEBUG=1"]},
                ):
                    with self.subTest(mutation=mutation), self.assertRaises(audit.AuditError):
                        check(dict(transform, **mutation))

    def test_closed_recipes_ignore_python_and_host_tool_overrides(self):
        poison = "__forbidden_doom_tool__"
        targets = [Path(source).with_suffix(".o").as_posix() for source in SOURCES]
        for host, suffix in (("Windows_NT", "exe"), ("Linux", "elf")):
            with self.subTest(host=host):
                result = subprocess.run([
                    "make", "-B", "-n", f"OS={host}",
                    *[f"{name}={poison}" for name in (
                        "PYTHON", "CUPIDC_KERNEL_COMPILE", "CUPIDC_KERNEL_COMPILE_INPUTS",
                        "CHECKED_SEED_RUN", "CC", "CXX", "CPP", "HOSTCC", "HOSTCXX",
                        "ASM", "AS", "LD", "AR", "NM", "OBJCOPY", "CFLAGS", "EXTRA_CFLAGS",
                        "PRODUCTION_SEED_INPUTS", "PRODUCTION_SEED_DIRECTORY", "PRODUCTION_SEED_SUFFIX")],
                    *targets], cwd=ROOT, text=True, capture_output=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn(poison, result.stdout + result.stderr)
                commands = [line for line in result.stdout.replace("\\\n", " ").splitlines()
                            if " compile-doom " in line]
                self.assertEqual(len(commands), 83)
                for source in SOURCES:
                    matches = [line for line in commands if f"--source {source} " in line]
                    self.assertEqual(len(matches), 1)
                    self.assertIn(f"cupidbuild.{suffix} compile-doom", matches[0])
                    self.assertIn(f"--output {Path(source).with_suffix('.o').as_posix()}", matches[0])
                    self.assertNotIn("--profile", matches[0])
                    self.assertNotIn("--timeout", matches[0])


if __name__ == "__main__":
    unittest.main()

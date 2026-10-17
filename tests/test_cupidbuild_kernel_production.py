import copy
import subprocess
import unittest
from pathlib import Path

from tools import build_graph_audit as audit
from tools import cupidc_kernel_compile as oracle


ROOT = Path(__file__).resolve().parents[1]


class CupidBuildKernelProductionTests(unittest.TestCase):
    def test_native_cohort_retains_every_frozen_header(self):
        self.assertEqual(
            audit._CUPIDBUILD_KERNEL_INPUT_CLOSURES,
            oracle.FROZEN_KERNEL_INPUT_CLOSURES,
        )
        self.assertEqual(len(audit._CUPIDBUILD_KERNEL_INPUT_CLOSURES), 157)

    def test_delivery_contract_rejects_recipe_and_input_changes(self):
        seed = ["seed/manifest.json"] + [
            f"seed/{tool}.elf"
            for tool in ("cupidc", "cupidasm", "cupiddis", "cupidld", "cupidobj", "cupidbuild")
        ]
        for source, headers in oracle.FROZEN_KERNEL_INPUT_CLOSURES.items():
            output = Path(source).with_suffix(".o").as_posix()
            transform = {
                "output": output,
                "operation": "compile_c_to_elf32_object",
                "tools": ["cupid_builder", "cupid_c_compiler"],
                "inputs": [source, *headers, "Makefile", *seed],
                "recipe": audit._cupidbuild_kernel_compile_recipe(source),
                "order_only_inputs": ["build/bootstrap/doom-cupidc-inputs.json"],
            }
            with self.subTest(source=source):
                audit._validate_cupidbuild_kernel_compile_delivery(
                    [transform], seed_inputs=seed
                )
                self.assertEqual(
                    audit._c_preprocessor_profile_for_c_transform(".", transform),
                    "KERNEL_I386",
                )
                for removed in transform["inputs"]:
                    changed = copy.deepcopy(transform)
                    changed["inputs"].remove(removed)
                    with self.assertRaises(audit.AuditError):
                        audit._validate_cupidbuild_kernel_compile_delivery(
                            [changed], seed_inputs=seed
                        )
                for mutation in (
                    {"tools": ["cupid_c_compiler", "host_python"]},
                    {"operation": "compile_c_to_host_object"},
                    {"inputs": transform["inputs"] + ["tools/cupidc_kernel_compile.py"]},
                    {"inputs": transform["inputs"] + [source]},
                    {"order_only_inputs": [headers[0]]},
                    {"recipe": transform["recipe"] + ["echo unchecked"]},
                    {"recipe": [line.replace(output, "kernel/wrong.o") for line in transform["recipe"]]},
                    {"recipe": transform["recipe"][:-1] + [transform["recipe"][-1] + " --timeout 1"]},
                    {"recipe": transform["recipe"][:-1] + [transform["recipe"][-1] + " --profile doom-tree"]},
                    {"recipe": transform["recipe"][:-1] + [transform["recipe"][-1] + " -DCHANGED=1"]},
                ):
                    with self.subTest(mutation=mutation), self.assertRaises(audit.AuditError):
                        audit._validate_cupidbuild_kernel_compile_delivery(
                            [dict(transform, **mutation)], seed_inputs=seed
                        )

    def test_closed_recipes_ignore_python_and_host_tool_overrides(self):
        poison = "__forbidden_tool__"
        targets = [
            Path(source).with_suffix(".o").as_posix()
            for source in oracle.FROZEN_KERNEL_INPUT_CLOSURES
        ]
        for host, suffix in (("Windows_NT", "exe"), ("Linux", "elf")):
            with self.subTest(host=host):
                result = subprocess.run(
                    [
                        "make", "-B", "-n", f"OS={host}",
                        "-o", "kernel/cpu/ksyms_data.cc",
                        *[f"{variable}={poison}" for variable in (
                            "PYTHON", "CUPIDC_KERNEL_COMPILE", "CUPIDC_KERNEL_COMPILE_INPUTS",
                            "CHECKED_SEED_RUN", "CC", "CXX", "CPP", "HOSTCC", "HOSTCXX",
                            "ASM", "AS", "LD", "AR", "NM", "OBJCOPY",
                            "CFLAGS", "EXTRA_CFLAGS", "PRODUCTION_SEED_INPUTS",
                            "PRODUCTION_SEED_DIRECTORY", "PRODUCTION_SEED_SUFFIX",
                        )],
                        *targets,
                    ],
                    cwd=ROOT, text=True, capture_output=True, timeout=30,
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertNotIn(poison, result.stdout + result.stderr)
                commands = [line for line in result.stdout.replace("\\\n", " ").splitlines()
                            if " compile-kernel " in line]
                self.assertEqual(len(commands), 157)
                for source in oracle.FROZEN_KERNEL_INPUT_CLOSURES:
                    command = next(line for line in commands if f"--source {source} " in line)
                    self.assertIn(f"cupidbuild.{suffix} compile-kernel", command)
                    self.assertIn(f"--output {Path(source).with_suffix('.o').as_posix()}", command)
                    self.assertNotIn("--timeout", command)
                    self.assertNotIn("--profile", command)


if __name__ == "__main__":
    unittest.main()

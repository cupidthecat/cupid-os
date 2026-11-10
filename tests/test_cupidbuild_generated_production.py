import copy
import subprocess
import unittest
from pathlib import Path

from tools import build_graph_audit as audit

ROOT = Path(__file__).resolve().parents[1]
SOURCES = audit._CUPIDBUILD_GENERATED_SOURCES


class CupidBuildGeneratedProductionTests(unittest.TestCase):
    def test_exact_three_delivery_and_kernel_profile(self):
        inputs = ["Makefile", "tools/cupidc_production_compile.py",
                  "tools/cupidc_kernel_compile.py", "tools/native_user_toolchain.py",
                  "tools/bootstrap_toolchain.py", "seed/manifest.json"]
        inputs += [f"seed/{tool}.elf" for tool in
                   ("cupidc", "cupidasm", "cupiddis", "cupidld", "cupidobj", "cupidbuild")]
        self.assertEqual(len(SOURCES), 3)
        for source in SOURCES:
            headers = ["drivers/serial.h", "kernel/core/types.h", "kernel/fs/homefs.h", "kernel/fs/ramfs.h", "kernel/fs/vfs.h"]
            value = {"output": Path(source).with_suffix(".o").as_posix(),
                     "operation": "compile_c_to_elf32_object",
                     "tools": ["cupid_builder", "cupid_c_compiler"],
                     "inputs": [source, *headers, *inputs],
                     "recipe": audit._cupidbuild_generated_compile_recipe(source),
                     "order_only_inputs": ["build/bootstrap/doom-cupidc-inputs.json"]}
            def check(item):
                audit._validate_cupidbuild_generated_compile_delivery([item], seed_inputs=inputs[5:])
            with self.subTest(source=source):
                check(value)
                self.assertEqual(audit._c_preprocessor_profile_for_c_transform(".", value), "KERNEL_I386")
                for removed in value["inputs"]:
                    changed = copy.deepcopy(value)
                    changed["inputs"].remove(removed)
                    with self.subTest(removed=removed), self.assertRaises(audit.AuditError):
                        check(changed)
                for change in (
                    {"tools": ["cupid_c_compiler", "host_python"]},
                    {"operation": "compile_c_to_host_object"},
                    {"inputs": value["inputs"] + [source]},
                    {"inputs": value["inputs"] + ["unapproved.h"]},
                    {"order_only_inputs": [source]},
                    {"order_only_inputs": []},
                    {"recipe": value["recipe"] + ["echo unchecked"]},
                    {"recipe": value["recipe"][:-1] + ["--source other.cc --output other.o"]},
                    {"recipe": value["recipe"][:-1] + [value["recipe"][-1] + " --timeout 1"]},
                    {"recipe": value["recipe"][:-1] + [value["recipe"][-1] + " -DDEBUG=0"]},
                ):
                    with self.subTest(change=change), self.assertRaises(audit.AuditError):
                        check(dict(value, **change))

    def test_three_closed_recipes_ignore_host_runner_and_flags(self):
        targets = [Path(source).with_suffix(".o").as_posix() for source in SOURCES]
        poison = "__forbidden_generated_tool__"
        for host, suffix in (("Windows_NT", "exe"), ("Linux", "elf")):
            result = subprocess.run([
                "make", "-B", "-n", f"OS={host}",
                *[f"{name}={poison}" for name in (
                    "PYTHON", "CUPIDC_PRODUCTION_COMPILE", "CUPIDC_PRODUCTION_COMPILE_INPUTS", "CHECKED_SEED_RUN",
                    "CC", "CXX", "CPP", "HOSTCC", "HOSTCXX", "ASM", "AS", "LD",
                    "AR", "NM", "OBJCOPY", "CFLAGS", "EXTRA_CFLAGS",
                    "PRODUCTION_SEED_INPUTS", "PRODUCTION_SEED_DIRECTORY", "PRODUCTION_SEED_SUFFIX")],
                *targets], cwd=ROOT, text=True, capture_output=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn(poison, result.stdout + result.stderr)
            commands = [line for line in result.stdout.replace("\\\n", " ").splitlines()
                        if " compile-production " in line]
            self.assertEqual(len(commands), 3)
            for source in SOURCES:
                matches = [line for line in commands if f"--source {source} " in line]
                self.assertEqual(len(matches), 1)
                self.assertIn(f"cupidbuild.{suffix} compile-production", matches[0])
                self.assertIn(f"--output {Path(source).with_suffix('.o').as_posix()}", matches[0])


if __name__ == "__main__":
    unittest.main()

import os
import re
import shlex
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]


def _make_compile_command(make_root, target, source):
    result = subprocess.run(
        [
            "make",
            "--no-print-directory",
            "-n",
            "-B",
            target,
            "WAD_SRCS=",
        ],
        cwd=make_root,
        text=True,
        capture_output=True,
    )
    if result.returncode != 0:
        raise AssertionError(result.stderr)
    commands = [
        line
        for line in result.stdout.splitlines()
        if source in line and target in line
    ]
    if not commands:
        raise AssertionError(f"missing dry-run compiler command for {target}")
    arguments = shlex.split(commands[-1], posix=os.name != "nt")
    if os.name == "nt":
        arguments = [
            argument[1:-1]
            if len(argument) >= 2
            and argument[0] == argument[-1]
            and argument[0] in {'"', "'"}
            else argument
            for argument in arguments
        ]
    return arguments


class KernelFpuCodeGenerationContractTests(unittest.TestCase):
    def test_cpu_enable_helper_compiles_without_live_fp_registers(self):
        with tempfile.TemporaryDirectory(prefix="cupid-fpu-codegen-") as directory:
            assembly = Path(directory) / "fpu.s"
            command = _make_compile_command(
                REPO_ROOT,
                "kernel/cpu/fpu.o",
                "kernel/cpu/fpu.c",
            )
            command[command.index("-c")] = "-S"
            command[command.index("-o") + 1] = str(assembly)

            result = subprocess.run(
                command,
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(
                result.returncode,
                0,
                "supported-host FPU code-generation contract failed\n"
                + result.stdout
                + result.stderr,
            )
            generated = assembly.read_text(encoding="utf-8")
            start = re.search(r"(?m)^fpu_init_cpu:.*$", generated)
            self.assertIsNotNone(start, "missing fpu_init_cpu assembly label")
            tail = generated[start.end() :]
            end = re.search(r"(?m)^\s*\.size\s+fpu_init_cpu\b", tail)
            self.assertIsNotNone(end, "missing fpu_init_cpu assembly extent")
            body = tail[: end.start()]
            self.assertNotRegex(
                body,
                r"%(?:xmm|ymm|zmm|mm)[0-9]+\b|%st(?:\([0-7]\))?",
                "compiler introduced an FP/SIMD register before per-CPU enablement",
            )
            enable = body.rfind("%cr4")
            initialize = body.find("fninit")
            load_control = body.find("ldmxcsr")
            self.assertGreaterEqual(enable, 0)
            self.assertNotRegex(
                body,
                r"(?mi)^[ \t]+call[a-z]*\b",
                "compiler introduced a runtime helper in the CPU enable helper",
            )
            pre_enable = body[:enable]
            self.assertNotRegex(
                pre_enable,
                r"(?mi)^[ \t]+f[a-z0-9]+\b",
                "compiler introduced an implicit-stack x87 instruction before enablement",
            )
            self.assertGreater(initialize, enable)
            self.assertGreater(load_control, initialize)


class FreestandingCodeGenerationPolicyTests(unittest.TestCase):
    def test_freestanding_objects_disable_unusable_unwind_tables(self):
        cases = [
            (REPO_ROOT, "kernel/core/kernel.o", "kernel/core/kernel.c"),
            (REPO_ROOT, "kernel/doom/src/am_map.o", "kernel/doom/src/am_map.c"),
            (REPO_ROOT / "user", "build/hello.o", "examples/hello.c"),
        ]
        for make_root, target, source in cases:
            with self.subTest(make_root=make_root.name, target=target):
                arguments = _make_compile_command(make_root, target, source)
                self.assertIn("-fno-asynchronous-unwind-tables", arguments)
                self.assertIn("-fno-unwind-tables", arguments)


if __name__ == "__main__":
    unittest.main()

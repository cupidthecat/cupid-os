import hashlib
import os
import re
import shlex
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools import cupidc_kernel_compile as kernel_compile
from tools.bootstrap_toolchain import freeze_seed_inputs, run_seed_tool


REPO_ROOT = Path(__file__).resolve().parents[1]
SEED_MANIFEST = (
    REPO_ROOT
    / "bootstrap"
    / "seeds"
    / "i386-linux"
    / "manifest.json"
)


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
    logical_output = re.sub(r"\\\r?\n[ \t]*", " ", result.stdout)
    commands = [
        line
        for line in logical_output.splitlines()
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
        if not SEED_MANIFEST.is_file():
            self.skipTest("checked seed manifest is not present")
        if os.name == "nt" and not shutil.which("wsl"):
            self.skipTest("WSL is not available")

        with tempfile.TemporaryDirectory(
            prefix=".cupid-fpu-codegen-",
            dir=REPO_ROOT,
        ) as directory:
            object_path = Path(directory) / "fpu.o"
            kernel_compile.compile_kernel_source(
                REPO_ROOT,
                REPO_ROOT / "kernel" / "cpu" / "fpu.cc",
                object_path,
            )
            object_image = object_path.read_bytes()
            self.assertEqual(len(object_image), 6484)
            self.assertEqual(
                hashlib.sha256(object_image).hexdigest(),
                "d732a346a4db31320379bfc7f48d1056bd5a37e164d33588b3c3b7ca9490a622",
            )

            with tempfile.TemporaryDirectory(
                prefix="cupid-fpu-disassembler-"
            ) as seed_directory:
                seed = freeze_seed_inputs(
                    SEED_MANIFEST,
                    Path(seed_directory),
                )
                result = run_seed_tool(
                    SEED_MANIFEST,
                    REPO_ROOT,
                    "cupiddis",
                    (
                        "--disassemble",
                        object_path.resolve(),
                    ),
                    timeout=kernel_compile.DEFAULT_TIMEOUT_SECONDS,
                    frozen_seed=seed,
                )
            self.assertEqual(
                result.returncode,
                0,
                "checked CupidDis could not decode the CupidC FPU object\n"
                + result.stdout
                + result.stderr,
            )
            self.assertEqual(result.stderr, "")
            start = re.search(
                r"(?m)^[0-9A-F]{8} <fpu_init_cpu>:$",
                result.stdout,
            )
            self.assertIsNotNone(start, "missing decoded fpu_init_cpu symbol")
            tail = result.stdout[start.end() :]
            end = re.search(r"(?m)^[0-9A-F]{8} <[^>]+>:$", tail)
            self.assertIsNotNone(end, "missing decoded fpu_init_cpu extent")
            body = tail[: end.start()]
            instructions = [
                line.split("  ", 2)[-1]
                for line in body.splitlines()
                if re.match(r"^[0-9A-F]{8}:", line)
            ]
            decoded_instructions = "\n".join(instructions)
            self.assertNotRegex(
                decoded_instructions,
                r"(?i)\b(?:xmm|ymm|zmm|mm)[0-9]+\b"
                r"|\bst(?:\([0-7]\)|[0-7])\b",
                "compiler introduced an FP/SIMD register before per-CPU enablement",
            )
            enable = instructions.index("mov cr4, eax")
            initialize = instructions.index("fninit")
            load_control = next(
                index
                for index, instruction in enumerate(instructions)
                if instruction.startswith("ldmxcsr ")
            )
            self.assertGreaterEqual(enable, 0)
            self.assertFalse(
                any(
                    re.match(r"^call\b", instruction)
                    for instruction in instructions
                ),
                "compiler introduced a runtime helper in the CPU enable helper",
            )
            self.assertFalse(
                any(
                    re.match(r"^f[a-z0-9]+\b", instruction)
                    for instruction in instructions[:enable]
                ),
                "compiler introduced an implicit-stack x87 instruction before enablement",
            )
            self.assertGreater(initialize, enable)
            self.assertGreater(load_control, initialize)


class FreestandingCodeGenerationPolicyTests(unittest.TestCase):
    def test_doom_objects_use_the_checked_cupidc_profile(self):
        arguments = _make_compile_command(
            REPO_ROOT,
            "kernel/doom/src/am_map.o",
            "kernel/doom/src/am_map.cc",
        )
        self.assertTrue(arguments[0].endswith(("/cupidbuild.exe", "/cupidbuild.elf")))
        self.assertIn("compile-doom", arguments)
        self.assertNotIn("-fno-asynchronous-unwind-tables", arguments)
        self.assertNotIn("-fno-unwind-tables", arguments)

    def test_checked_cupidc_freestanding_objects_avoid_host_unwind_flags(self):
        arguments = _make_compile_command(
            REPO_ROOT,
            "kernel/core/kernel.o",
            "kernel/core/kernel.cc",
        )
        self.assertTrue(arguments[0].endswith(("/cupidbuild.exe", "/cupidbuild.elf")))
        self.assertIn("compile-kernel", arguments)
        self.assertNotIn("-fno-asynchronous-unwind-tables", arguments)
        self.assertNotIn("-fno-unwind-tables", arguments)

    def test_user_objects_use_the_checked_cupidc_profile(self):
        arguments = _make_compile_command(
            REPO_ROOT / "user",
            "build/hello.o",
            "examples/hello.cc",
        )
        self.assertTrue(arguments[0].endswith(("/cupidbuild.exe", "/cupidbuild.elf")))
        self.assertIn("compile-user", arguments)
        self.assertNotIn("-fno-asynchronous-unwind-tables", arguments)


if __name__ == "__main__":
    unittest.main()

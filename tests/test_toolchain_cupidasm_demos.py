import os
import subprocess
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"

EXPECTED_DEMOS = (
    "demos/asm_compat_reserve.asm",
    "demos/bubblesort.asm",
    "demos/data.asm",
    "demos/factorial.asm",
    "demos/fibonacci.asm",
    "demos/fpu_kernel.asm",
    "demos/fs_syscalls.asm",
    "demos/hello.asm",
    "demos/include_feature.asm",
    "demos/include_helper.asm",
    "demos/jcc_aliases.asm",
    "demos/loop.asm",
    "demos/math.asm",
    "demos/parity_core.asm",
    "demos/parity_diag.asm",
    "demos/parity_gfx2d.asm",
    "demos/parity_priv.asm",
    "demos/reserve_directives.asm",
    "demos/simd_blur.asm",
    "demos/stack.asm",
    "demos/syscall_table_demo.asm",
    "demos/syscall_vfs_extended_demo.asm",
)


class CupidAsmDemoCorpusTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls._build_directory = tempfile.TemporaryDirectory(
            prefix=".cupidasm-demos-build-", dir=TOOLCHAIN_ROOT
        )
        build_path = Path(cls._build_directory.name)
        relative_build = build_path.relative_to(TOOLCHAIN_ROOT).as_posix()
        suffix = ".exe" if os.name == "nt" else ""
        cls.contract_path = build_path / ("cupidasm-demos-contract" + suffix)
        target = relative_build + "/cupidasm-demos-contract" + suffix
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
        if result.returncode != 0:
            cls._build_directory.cleanup()
            raise AssertionError(
                "CupidASM demo contract build failed\n"
                + result.stdout
                + result.stderr
            )

    @classmethod
    def tearDownClass(cls):
        cls._build_directory.cleanup()

    def _run_contract(self, mode):
        return subprocess.run(
            [str(self.contract_path), mode],
            cwd=TOOLCHAIN_ROOT,
            text=True,
            capture_output=True,
        )

    def test_contract_and_active_directory_name_the_same_22_demo_sources(self):
        result = self._run_contract("list")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(tuple(result.stdout.splitlines()), EXPECTED_DEMOS)
        active = tuple(
            sorted(
                path.relative_to(REPO_ROOT).as_posix()
                for path in (REPO_ROOT / "demos").glob("*.asm")
            )
        )
        self.assertEqual(active, EXPECTED_DEMOS)

    def test_all_unmodified_demos_assemble_as_deterministic_fixed_images(self):
        result = self._run_contract("all")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        expected_lines = tuple(f"{path}: ok" for path in EXPECTED_DEMOS) + (
            "demos: 22 ok",
        )
        self.assertEqual(tuple(result.stdout.splitlines()), expected_lines)


if __name__ == "__main__":
    unittest.main()

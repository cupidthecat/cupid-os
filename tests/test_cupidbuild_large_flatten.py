import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests import test_toolchain_cupidbuild as cupidbuild_cli


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"


class CupidBuildLargeFlattenTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        temporary = tempfile.TemporaryDirectory(
            prefix=".cupidbuild-large-flatten-build-", dir=TOOLCHAIN_ROOT
        )
        cls.addClassCleanup(temporary.cleanup)
        build = Path(temporary.name)
        relative = build.relative_to(TOOLCHAIN_ROOT).as_posix()
        suffix = ".exe" if os.name == "nt" else ""
        cls.cli_path = build / ("cupidbuild" + suffix)
        result = subprocess.run(
            [
                "make", "-C", str(TOOLCHAIN_ROOT),
                f"BUILD_DIR={relative}", f"{relative}/cupidbuild{suffix}",
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=180,
        )
        if result.returncode != 0:
            raise AssertionError(
                "CupidBuild hosted CLI build failed\n"
                + result.stdout + result.stderr
            )

    def setUp(self):
        self.helper = cupidbuild_cli.CupidBuildCliTests()
        self.helper.cli_path = self.cli_path

    def _fixture(self):
        temporary = tempfile.TemporaryDirectory(
            prefix=".cupidbuild large flatten ", dir=REPO_ROOT
        )
        self.addCleanup(temporary.cleanup)
        root = Path(temporary.name)
        self.assertIn(" ", root.name)
        kernel = root / "kernel"
        kernel.mkdir()
        seed_manifest = self.helper._copy_checked_assembly_seed(root / "seed")
        built = self.helper._build_ksyms_elf(
            root,
            "BITS 32\n"
            "global _start:function\n"
            "section .text\n"
            "_start:\n"
            "    mov eax, 0x12345678\n"
            "    ret\n",
        )
        inputs = ["kernel/kernel.elf.pass1", "kernel/kernel.elf"]
        inputs.extend(f"kernel/cohort-{index:03d}.elf" for index in range(498))
        self.assertEqual(len(inputs), 500)
        self.assertEqual(len(set(inputs)), 500)
        for relative in inputs:
            shutil.copy2(built, root / relative)
        manifest = root / "code-inputs.txt"
        manifest.write_text("\n".join(inputs) + "\n", encoding="ascii", newline="\n")

        # These absolute frozen names exceed Windows's 32,767-character limit.
        # The public transaction must still accept the complete 500-input cohort.
        absolute_arguments = [
            str(root / ".cupidbuild-object-00000000" / f"code-{index:03d}.bin")
            for index in range(500)
        ]
        quoted_length = sum(len(argument) + 3 for argument in absolute_arguments)
        self.assertGreater(quoted_length, 32767)
        return root, manifest, kernel / "kernel.bin", seed_manifest, inputs

    def test_full_500_input_cohort_publishes_and_repeats_exact_kernel_bytes(self):
        root, manifest, output, seed_manifest, _inputs = self._fixture()
        expected = root / "expected.bin"
        result = subprocess.run(
            [
                str(self.helper._production_tool("cupidobj")),
                "flat", str(root / "kernel/kernel.elf"), "-o", str(expected),
            ],
            cwd=root,
            text=True,
            capture_output=True,
            timeout=90,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        expected_bytes = expected.read_bytes()
        self.assertTrue(expected_bytes)
        expected_entries = set(root.rglob("*")) | {output}

        for _ in range(2):
            result = self.helper._run_flatten_kernel(
                root, manifest, output, seed_manifest
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual((result.stdout, result.stderr), ("", ""))
            self.assertEqual(output.read_bytes(), expected_bytes)
            self.assertEqual(self.helper._private_roots(root), set())
            self.assertEqual(set(root.rglob("*")), expected_entries)

    def test_invalid_last_input_is_inspected_and_preserves_the_previous_kernel(self):
        root, manifest, output, seed_manifest, inputs = self._fixture()
        self.assertNotIn(inputs[-1], ("kernel/kernel.elf.pass1", "kernel/kernel.elf"))
        (root / inputs[-1]).write_bytes(b"not an ELF image\n")
        sentinel = b"last known good flat kernel"
        output.write_bytes(sentinel)
        stable_time = 1_700_000_000_000_000_000
        os.utime(output, ns=(stable_time, stable_time))
        expected_entries = set(root.rglob("*"))

        result = self.helper._run_flatten_kernel(
            root, manifest, output, seed_manifest
        )

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("checked CupidDis failed", result.stderr)
        self.assertNotIn("could not be started", result.stderr)
        self.assertIn("input is not supported ELF32 or PE32", result.stderr)
        if os.name == "nt":
            self.assertIn("code-499.bin", result.stderr)
        self.assertEqual(output.read_bytes(), sentinel)
        self.assertEqual(output.stat().st_mtime_ns, stable_time)
        self.assertEqual(self.helper._private_roots(root), set())
        self.assertEqual(set(root.rglob("*")), expected_entries)


if __name__ == "__main__":
    unittest.main()

import hashlib
import os
import shutil
import subprocess
import tempfile
import time
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOLCHAIN_ROOT = REPO_ROOT / "toolchain"
NATIVE_ORACLE_NAMES = (
    "core-contract",
    "cupidc-pp-contract",
    "cupidc-type-contract",
    "cupidc-frontend-contract",
    "cupidc-ir-contract",
    "cupidc-object-contract",
    "cupidc",
    "elf32-contract",
    "x86-contract",
    "cupiddis-contract",
    "cupiddis",
    "cupidasm-contract",
    "cupidasm-demos-contract",
    "cupidasm-kernel-elf-contract",
    "cupidasm",
    "cupidobj-contract",
    "cupidobj",
    "cupidld-contract",
    "cupidld",
)


class ToolchainReproducibilityTests(unittest.TestCase):
    def test_repeated_host_links_are_byte_reproducible(self):
        build_path = Path(
            tempfile.mkdtemp(prefix=".repro-build-", dir=TOOLCHAIN_ROOT)
        )
        relative_build = build_path.relative_to(TOOLCHAIN_ROOT).as_posix()
        suffix = ".exe" if os.name == "nt" else ""
        core_artifact = f"{relative_build}/core-contract{suffix}"
        native_artifacts = tuple(
            f"{relative_build}/{name}{suffix}"
            for name in NATIVE_ORACLE_NAMES
        )

        def run_make(*goals):
            result = subprocess.run(
                [
                    "make",
                    "-C",
                    str(TOOLCHAIN_ROOT),
                    f"BUILD_DIR={relative_build}",
                    *goals,
                ],
                cwd=REPO_ROOT,
                text=True,
                capture_output=True,
            )
            self.assertEqual(
                result.returncode,
                0,
                "hosted toolchain build failed\n"
                + result.stdout
                + result.stderr,
            )

        def build_manifest(clean):
            if clean:
                run_make("clean")
            run_make("native-oracles")
            return {
                artifact: hashlib.sha256(
                    (TOOLCHAIN_ROOT / artifact).read_bytes()
                ).hexdigest()
                for artifact in native_artifacts
            }

        try:
            first_manifest = build_manifest(clean=True)
            for artifact in first_manifest:
                (TOOLCHAIN_ROOT / artifact).unlink()
            if os.name == "nt":
                # The PE/COFF timestamp has one-second resolution. Cross a timestamp
                # boundary so a nondeterministic linker cannot make this test pass
                # merely because both links happened within the same second.
                time.sleep(1.1)
            second_manifest = build_manifest(clean=False)
            self.assertEqual(
                second_manifest,
                first_manifest,
                "clean hosted-toolchain links produced different bytes",
            )

            run_make("clean")
            run_make(core_artifact)
            clean_core_digest = hashlib.sha256(
                (TOOLCHAIN_ROOT / core_artifact).read_bytes()
            ).hexdigest()
            self.assertEqual(
                clean_core_digest,
                first_manifest[core_artifact],
                "fresh host object timestamps changed the linked executable",
            )
        finally:
            shutil.rmtree(build_path, ignore_errors=True)


if __name__ == "__main__":
    unittest.main()

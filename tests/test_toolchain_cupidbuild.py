## @file
#  @brief Run CupidBuild CLI cases with one shared build on each host.
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.cupidbuild_assembly_cases import AssemblyPublicationCases
from tests.cupidbuild_cli_fixtures import BASELINE_JPEG, REPO_ROOT, TOOLCHAIN_ROOT, CupidBuildCliFixtures
from tests.cupidbuild_image_cases import ImagePublicationCases
from tests.cupidbuild_profile_cases import ProfilePublicationCases
from tests.cupidbuild_runner_cases import CheckedRunnerCases
from tests.cupidbuild_seed_cases import SeedContractCases
from tools.bootstrap_toolchain import (
    _CUPIDBUILD_BOOTLOADER_BEHAVIOR_SOURCE,
    _CUPIDBUILD_ISO_PATTERN_BEHAVIOR_SOURCE,
    _CUPIDBUILD_SMP_BEHAVIOR_SOURCE,
)


class CupidBuildCliTests(
    ProfilePublicationCases,
    ImagePublicationCases,
    CheckedRunnerCases,
    SeedContractCases,
    AssemblyPublicationCases,
    CupidBuildCliFixtures,
    unittest.TestCase,
):
    @classmethod
    def setUpClass(cls):
        cls._build_directory = tempfile.TemporaryDirectory(
            prefix=".cupidbuild-cli-build-", dir=TOOLCHAIN_ROOT
        )
        build_path = Path(cls._build_directory.name)
        relative_build = build_path.relative_to(TOOLCHAIN_ROOT)
        suffix = ".exe" if os.name == "nt" else ""
        cls.cli_path = build_path / ("cupidbuild" + suffix)
        cli_target = relative_build.as_posix() + "/cupidbuild" + suffix
        result = subprocess.run(
            [
                "make",
                "-C",
                str(TOOLCHAIN_ROOT),
                f"BUILD_DIR={relative_build.as_posix()}",
                cli_target,
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        if result.returncode != 0:
            cls._build_directory.cleanup()
            raise AssertionError(
                "CupidBuild hosted CLI build failed\n"
                + result.stdout
                + result.stderr
            )
        cls._race_build_directory = None
        cls.race_cli_path = None
        cls._race_build_directory = tempfile.TemporaryDirectory(
            prefix=".cupidbuild-cli-race-build-", dir=TOOLCHAIN_ROOT
        )
        race_build_path = Path(cls._race_build_directory.name)
        race_relative_build = race_build_path.relative_to(TOOLCHAIN_ROOT)
        cls.race_cli_path = race_build_path / ("cupidbuild" + suffix)
        race_target = race_relative_build.as_posix() + "/cupidbuild" + suffix
        race_result = subprocess.run(
            [
                "make",
                "-C",
                str(TOOLCHAIN_ROOT),
                f"BUILD_DIR={race_relative_build.as_posix()}",
                "CPPFLAGS=-DCUPIDBUILD_PROFILE_PARENT_RACE_TEST "
                "-DCUPIDBUILD_PROFILE_DIRECTORY_RACE_TEST "
                "-DCUPIDBUILD_PUBLICATION_RACE_TEST "
                "-DCUPIDBUILD_NOREPLACE_RACE_TEST",
                race_target,
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )
        if race_result.returncode != 0:
            cls._race_build_directory.cleanup()
            cls._build_directory.cleanup()
            raise AssertionError(
                "CupidBuild profile race CLI build failed\n"
                + race_result.stdout
                + race_result.stderr
            )

    @classmethod
    def tearDownClass(cls):
        if cls._race_build_directory is not None:
            cls._race_build_directory.cleanup()
        cls._build_directory.cleanup()

    def test_help_names_the_typed_jpeg_transaction(self):
        result = subprocess.run(
            [str(self.cli_path), "--help"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertRegex(
            result.stdout,
            r"cupidbuild embed-jpeg --seed-manifest MANIFEST\s+"
            r"--root ROOT --source SOURCE --output OUTPUT",
        )

    def test_help_names_the_typed_kernel_symbol_transaction(self):
        result = subprocess.run(
            [str(self.cli_path), "--help"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertRegex(
            result.stdout,
            r"cupidbuild generate-ksyms --seed-manifest MANIFEST\s+"
            r"--root ROOT --source SOURCE --output OUTPUT",
        )

    def test_help_names_the_typed_kernel_flatten_transaction(self):
        result = subprocess.run(
            [str(self.cli_path), "--help"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertRegex(
            result.stdout,
            r"cupidbuild flatten-kernel --seed-manifest MANIFEST\s+"
            r"--root ROOT --input-manifest MANIFEST --output OUTPUT",
        )

    def test_help_names_the_typed_profile_manifest_transaction(self):
        result = subprocess.run(
            [str(self.cli_path), "--help"],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
        )

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertRegex(
            result.stdout,
            r"cupidbuild generate-profile-manifest "
            r"--seed-manifest MANIFEST\s+--root ROOT --output OUTPUT",
        )


if __name__ == "__main__":
    unittest.main()

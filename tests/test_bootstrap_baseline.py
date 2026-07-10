import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest
from copy import deepcopy
from pathlib import Path
from unittest.mock import patch

from tools import bootstrap_baseline


REPO_ROOT = Path(__file__).resolve().parents[1]


def _write_test_elf(path, text=b"TEXT!"):
    strings = b"\0.shstrtab\0.text\0"
    section_offset = 52
    strings_offset = section_offset + 3 * 40
    text_offset = strings_offset + len(strings)
    image = bytearray(text_offset + len(text))
    image[0:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<I", image, 32, section_offset)
    struct.pack_into("<H", image, 46, 40)
    struct.pack_into("<H", image, 48, 3)
    struct.pack_into("<H", image, 50, 1)
    struct.pack_into(
        "<IIIIIIIIII",
        image,
        section_offset + 40,
        1,
        3,
        0,
        0,
        strings_offset,
        len(strings),
        0,
        0,
        1,
        0,
    )
    struct.pack_into(
        "<IIIIIIIIII",
        image,
        section_offset + 80,
        11,
        1,
        6,
        0,
        text_offset,
        len(text),
        0,
        0,
        16,
        0,
    )
    image[strings_offset:text_offset] = strings
    image[text_offset:] = text
    path.write_bytes(image)


class BaselineArtifactTests(unittest.TestCase):
    def test_artifact_manifest_is_sorted_and_content_addressed(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "z.bin").write_bytes(b"")
            (root / "nested").mkdir()
            (root / "nested" / "a.bin").write_bytes(b"abc")

            manifest = bootstrap_baseline.hash_artifacts(
                root,
                ["z.bin", "nested/a.bin"],
            )

            self.assertEqual(
                [entry["path"] for entry in manifest["files"]],
                ["nested/a.bin", "z.bin"],
            )
            self.assertEqual(manifest["files"][0]["size"], 3)
            self.assertEqual(
                manifest["files"][0]["sha256"],
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
            )
            self.assertEqual(
                manifest["files"][1]["sha256"],
                "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
            )
            self.assertRegex(manifest["digest"], r"^[0-9a-f]{64}$")

    def test_comparison_reports_the_artifact_that_changed(self):
        first = {
            "digest": "first",
            "files": [
                {"path": "kernel/kernel.bin", "size": 3, "sha256": "aaa"},
                {"path": "boot/boot.bin", "size": 2, "sha256": "same"},
            ],
        }
        second = {
            "digest": "second",
            "files": [
                {"path": "boot/boot.bin", "size": 2, "sha256": "same"},
                {"path": "kernel/kernel.bin", "size": 4, "sha256": "bbb"},
            ],
        }

        comparison = bootstrap_baseline.compare_artifact_manifests(first, second)

        self.assertFalse(comparison["matched"])
        self.assertEqual(
            comparison["mismatches"],
            [
                {
                    "path": "kernel/kernel.bin",
                    "first": {"size": 3, "sha256": "aaa"},
                    "second": {"size": 4, "sha256": "bbb"},
                }
            ],
        )

    def test_elf32_section_size_reads_text_without_host_objdump(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "kernel.elf"
            _write_test_elf(path)

            self.assertEqual(bootstrap_baseline.elf32_section_size(path, ".text"), 5)

    def test_artifacts_are_hashed_before_runtime_can_mutate_the_image(self):
        with tempfile.TemporaryDirectory() as td:
            root = Path(td)
            (root / "kernel").mkdir()
            _write_test_elf(root / "kernel" / "kernel.elf")
            (root / "kernel" / "kernel.bin").write_bytes(b"kernel")
            image = root / "cupidos.img"
            image.write_bytes(b"clean")
            artifacts = ["kernel/kernel.elf", "kernel/kernel.bin", "cupidos.img"]
            (root / "user/build").mkdir(parents=True)
            (root / "user/build/hello").write_bytes(b"user")
            (root / "toolchain/build").mkdir(parents=True)
            (root / "toolchain/build/cupidasm").write_bytes(b"tool")
            commands = {}

            def invoke(check):
                commands[check.name] = check.command
                if check.name == "artifact-list":
                    return bootstrap_baseline.CommandResult(0, 0.1, json.dumps(artifacts))
                if check.name == "user-artifact-list":
                    return bootstrap_baseline.CommandResult(
                        0, 0.1, json.dumps(["build/hello"])
                    )
                if check.name == "toolchain-artifact-list":
                    return bootstrap_baseline.CommandResult(
                        0, 0.1, json.dumps(["build/cupidasm"])
                    )
                if check.name in {"cupidc-gui-smoke", "cupidasm-gui-smoke"}:
                    image.write_bytes(b"mutated")
                return bootstrap_baseline.CommandResult(0, 0.1, "ok\n")

            build = bootstrap_baseline.capture_build(
                root,
                run_index=1,
                jobs=1,
                tool_commands={
                    "make": ("make",),
                    "python": ("python",),
                    "qemu": ("qemu",),
                },
                invoke=invoke,
            )

            image_entry = next(
                entry
                for entry in build["artifacts"]["files"]
                if entry["path"] == "cupidos.img"
            )
            self.assertEqual(
                image_entry["sha256"],
                "3b066804f6d1d077173cfe4d06002e6a61e6f21c2b2e648417962115f1afcd8e",
            )
            self.assertEqual(image.read_bytes(), b"mutated")
            self.assertEqual(
                set(build["supported_roots"]),
                {"root", "user", "toolchain"},
            )
            self.assertEqual(
                {entry["path"] for entry in build["artifacts"]["files"]},
                {
                    "cupidos.img",
                    "kernel/kernel.bin",
                    "kernel/kernel.elf",
                    "toolchain/build/cupidasm",
                    "user/build/hello",
                },
            )
            self.assertEqual(
                commands["user-build"],
                ("make", "-C", "user", "-j1", "all"),
            )
            self.assertEqual(
                commands["toolchain-build"],
                ("make", "-C", "toolchain", "-j1", "all"),
            )
            validation, _, logical_ids, physical_paths = (
                bootstrap_baseline._build_artifact_evidence("test build", build)
            )
            self.assertEqual(validation, [])
            self.assertEqual(len(logical_ids), 5)
            self.assertEqual(len(physical_paths), 5)
            for name in ("cupidc-gui-smoke", "cupidasm-gui-smoke"):
                command = commands[name]
                pause = command.index("--key-pause")
                self.assertEqual(command[pause + 1], "0.60")


class BaselinePackageIsolationTests(unittest.TestCase):
    def test_repo_tools_package_wins_over_an_installed_name_collision(self):
        with tempfile.TemporaryDirectory(prefix="cupid-tools-shadow-") as directory:
            shadow_root = Path(directory)
            shadow_package = shadow_root / "tools"
            shadow_package.mkdir()
            (shadow_package / "__init__.py").write_text(
                'raise RuntimeError("shadow tools package imported")\n',
                encoding="utf-8",
            )
            environment = os.environ.copy()
            environment["PYTHONPATH"] = os.pathsep.join(
                filter(
                    None,
                    [str(shadow_root), environment.get("PYTHONPATH", "")],
                )
            )

            result = subprocess.run(
                [
                    sys.executable,
                    "-c",
                    "from tools import bootstrap_baseline; "
                    "print(bootstrap_baseline.__file__)",
                ],
                cwd=REPO_ROOT,
                env=environment,
                text=True,
                capture_output=True,
            )

            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(
                Path(result.stdout.strip()).resolve(),
                (REPO_ROOT / "tools/bootstrap_baseline.py").resolve(),
            )


class BaselineCheckTests(unittest.TestCase):
    def test_three_root_manifest_uses_a_versioned_schema(self):
        self.assertEqual(
            bootstrap_baseline.SCHEMA,
            "cupid.bootstrap-baseline.v2",
        )

    def test_command_output_normalization_does_not_treat_dot_as_a_path(self):
        result = bootstrap_baseline._run_command(
            bootstrap_baseline.CheckSpec(
                "relative-cwd",
                (sys.executable, "-c", "print('artifact.o')"),
            ),
            Path("."),
        )

        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.output.strip(), "artifact.o")

    def test_preflight_records_git_as_a_required_orchestration_tool(self):
        spec = bootstrap_baseline._tool_specs()["git"]

        self.assertEqual(spec.default, "git")
        self.assertEqual(spec.environment, "GIT")
        self.assertEqual(spec.version_args, ("--version",))

    def test_preflight_excludes_retired_normal_build_tools(self):
        specs = bootstrap_baseline._tool_specs()

        self.assertNotIn("assembler", specs)
        self.assertNotIn("linker", specs)
        self.assertNotIn("object_copy", specs)

    def test_nasm_is_recorded_as_an_optional_oracle_tool(self):
        specs = bootstrap_baseline._optional_oracle_tool_specs()

        self.assertEqual(set(specs), {"nasm"})
        self.assertEqual(specs["nasm"].default, "nasm")
        self.assertEqual(specs["nasm"].environment, "NASM")
        self.assertEqual(specs["nasm"].version_args, ("-v",))

    def test_optional_oracle_command_honors_the_recorded_environment(self):
        with patch.dict(
            "os.environ", {"NASM": f'"{sys.executable}" --oracle-mode'}
        ):
            commands = bootstrap_baseline.optional_oracle_commands()

        self.assertEqual(commands["nasm"], (sys.executable, "--oracle-mode"))

    def test_failed_check_is_recorded_and_dependent_checks_are_skipped(self):
        calls = []

        def invoke(check):
            calls.append(check.name)
            if check.name == "build":
                return bootstrap_baseline.CommandResult(1, 2.5, "compiler failed\n")
            return bootstrap_baseline.CommandResult(0, 1.0, "ok\n")

        checks = [
            bootstrap_baseline.CheckSpec("prepare", ("make", "distclean")),
            bootstrap_baseline.CheckSpec("build", ("make", "all")),
            bootstrap_baseline.CheckSpec("runtime", ("python", "smoke.py")),
        ]

        results = bootstrap_baseline.run_check_sequence(checks, invoke)

        self.assertEqual(calls, ["prepare", "build"])
        self.assertEqual(
            [result["status"] for result in results],
            ["pass", "fail", "skipped"],
        )
        self.assertEqual(results[1]["output_tail"], ["compiler failed"])
        self.assertEqual(results[2]["reason"], "blocked by failed check: build")

    def test_freestanding_i386_probe_requires_a_real_output_object(self):
        def successful_compile(command, **_kwargs):
            output = Path(command[command.index("-o") + 1])
            image = bytearray(52)
            image[:7] = b"\x7fELF\x01\x01\x01"
            struct.pack_into("<HHI", image, 16, 1, 3, 1)
            struct.pack_into("<H", image, 40, 52)
            output.write_bytes(image)
            return subprocess.CompletedProcess(command, 0, "")

        with patch.object(
            bootstrap_baseline.subprocess,
            "run",
            side_effect=successful_compile,
        ):
            result = bootstrap_baseline._probe_freestanding_i386(
                ("cc",), ("--target=i386-unknown-elf",)
            )

        self.assertEqual(result["status"], "pass")
        self.assertIn("-m32", result["command"])
        self.assertIn("-ffreestanding", result["command"])
        self.assertTrue(result["valid_i386_elf32_relocatable"])
        temp_root = str(Path(tempfile.gettempdir()).resolve())
        self.assertFalse(
            any(str(argument).startswith(temp_root) for argument in result["command"])
        )

    def test_freestanding_i386_probe_rejects_a_compiler_without_multilib(self):
        completed = subprocess.CompletedProcess(
            ["cc"], 1, "fatal error: 32-bit target unavailable\n"
        )
        with patch.object(
            bootstrap_baseline.subprocess,
            "run",
            return_value=completed,
        ):
            result = bootstrap_baseline._probe_freestanding_i386(("cc",), ())

        self.assertEqual(result["status"], "fail")
        self.assertEqual(result["output_tail"], ["fatal error: 32-bit target unavailable"])

    def test_freestanding_i386_probe_rejects_a_non_i386_object(self):
        def wrong_target_compile(command, **_kwargs):
            output = Path(command[command.index("-o") + 1])
            image = bytearray(64)
            image[:7] = b"\x7fELF\x02\x01\x01"
            output.write_bytes(image)
            return subprocess.CompletedProcess(command, 0, "")

        with patch.object(
            bootstrap_baseline.subprocess,
            "run",
            side_effect=wrong_target_compile,
        ):
            result = bootstrap_baseline._probe_freestanding_i386(("cc",), ())

        self.assertEqual(result["status"], "fail")
        self.assertTrue(result["produced_object"])
        self.assertFalse(result["valid_i386_elf32_relocatable"])

    def test_linux_default_output_uses_the_platform_cohort_name(self):
        with patch.object(bootstrap_baseline.platform, "system", return_value="Linux"):
            with patch.object(
                bootstrap_baseline.platform, "machine", return_value="x86_64"
            ):
                output = bootstrap_baseline._default_output(REPO_ROOT)

        self.assertEqual(output, REPO_ROOT / "build/bootstrap/linux-x86_64.json")

    def test_linux_host_metadata_records_distribution_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            os_release = Path(directory) / "os-release"
            os_release.write_text(
                'ID=ubuntu\nVERSION_ID="24.04"\nPRETTY_NAME="Ubuntu 24.04.2 LTS"\n',
                encoding="utf-8",
            )
            with patch.object(
                bootstrap_baseline.platform, "system", return_value="Linux"
            ):
                metadata = bootstrap_baseline.host_metadata(os_release)

        self.assertEqual(
            metadata["distribution"],
            {
                "id": "ubuntu",
                "version_id": "24.04",
                "pretty_name": "Ubuntu 24.04.2 LTS",
            },
        )

    def test_invalid_utf8_evidence_reports_a_controlled_error(self):
        with tempfile.TemporaryDirectory() as directory:
            evidence = Path(directory) / "invalid.json"
            evidence.write_bytes(b"\xff\xfe")

            with self.assertRaisesRegex(
                bootstrap_baseline.BaselineError,
                "cannot read baseline evidence",
            ):
                bootstrap_baseline._read_json(evidence)


class BaselineHostComparisonTests(unittest.TestCase):
    def artifact_manifest(self, files):
        files = sorted(files, key=lambda entry: entry["path"])
        return {
            "digest": bootstrap_baseline._artifact_digest(files),
            "files": files,
        }

    def baseline(self, system, artifact_hash):
        tool_path = "build/cupidasm.exe" if system == "Windows" else "build/cupidasm"
        root_files = [
            {"path": "cupidos.img", "size": 209715200, "sha256": artifact_hash},
            {"path": "kernel/kernel.elf", "size": 6200000, "sha256": artifact_hash},
            {"path": "kernel/kernel.bin", "size": 6000000, "sha256": artifact_hash},
        ]
        user_files = [
            {"path": "build/hello", "size": 1000, "sha256": artifact_hash}
        ]
        tool_files = [
            {"path": tool_path, "size": 1000, "sha256": artifact_hash}
        ]
        combined_files = [
            *(dict(entry) for entry in root_files),
            *(
                {**entry, "path": f"user/{entry['path']}"}
                for entry in user_files
            ),
            *(
                {**entry, "path": f"toolchain/{entry['path']}"}
                for entry in tool_files
            ),
        ]
        build = {
            "run": 1,
            "status": "pass",
            "checks": [
                {"name": name, "status": "pass"}
                for name in bootstrap_baseline.REQUIRED_BASELINE_CHECKS
            ],
            "artifacts": self.artifact_manifest(combined_files),
            "supported_roots": {
                "root": {
                    "path": ".",
                    "artifact_order": [entry["path"] for entry in root_files],
                    "artifacts": self.artifact_manifest(root_files),
                },
                "user": {
                    "path": "user",
                    "artifact_order": [entry["path"] for entry in user_files],
                    "artifacts": self.artifact_manifest(user_files),
                },
                "toolchain": {
                    "path": "toolchain",
                    "artifact_order": [entry["path"] for entry in tool_files],
                    "artifacts": self.artifact_manifest(tool_files),
                },
            },
            "artifact_cohort": [
                {"id": "root:cupidos.img", "path": "cupidos.img"},
                {
                    "id": "root:kernel/kernel.elf",
                    "path": "kernel/kernel.elf",
                },
                {
                    "id": "root:kernel/kernel.bin",
                    "path": "kernel/kernel.bin",
                },
                {"id": "user:build/hello", "path": "user/build/hello"},
                {
                    "id": "toolchain:build/cupidasm",
                    "path": f"toolchain/{tool_path}",
                },
            ],
        }
        quality = {
            "kernel_text_bytes": 1400000,
            "kernel_elf_bytes": 6200000,
            "kernel_binary_bytes": 6000000,
            "disk_image_bytes": 209715200,
        }
        host = {"system": system, "machine": "x86_64"}
        if system == "Linux":
            host["distribution"] = {
                "id": "ubuntu",
                "version_id": "24.04",
                "pretty_name": "Ubuntu 24.04 LTS",
            }
        tools = {
            name: {
                "status": "pass",
                "command": [name],
                "executable": f"/tools/{name}",
                "executable_sha256": "f" * 64,
                "version": f"{name} version 1",
                "version_returncode": 0,
            }
            for name in bootstrap_baseline.REQUIRED_HOST_TOOLS
        }
        tools["c_compiler"]["capabilities"] = {
            "freestanding_i386": {
                "status": "pass",
                "command": ["cc", "-m32"],
                "returncode": 0,
                "produced_object": True,
                "valid_i386_elf32_relocatable": True,
            }
        }
        if system == "Linux":
            tools["c_compiler"].update(
                {"executable": "/usr/bin/gcc", "version": "gcc 13.3.0"}
            )
            tools["symbol_reader"].update(
                {
                    "executable": "/usr/bin/nm",
                    "version": "GNU nm (GNU Binutils) 2.42",
                }
            )
        else:
            tools["c_compiler"].update(
                {"executable": "clang.exe", "version": "clang version 22.1.0"}
            )
            tools["symbol_reader"].update(
                {"executable": "llvm-nm.exe", "version": "LLVM version 22.1.0"}
            )
        second_build = deepcopy(build)
        second_build["run"] = 2
        supported_check_count = len(bootstrap_baseline.SUPPORTED_ROOT_CHECKS)
        second_build["checks"] = second_build["checks"][:supported_check_count]
        return {
            "schema": bootstrap_baseline.SCHEMA,
            "status": "pass",
            "source": {"revision": "a" * 40},
            "host": host,
            "tools": tools,
            "builds": [build, second_build],
            "reproducibility": {
                "matched": True,
                "reference_run": 1,
                "compared_runs": [2],
                "mismatches": [],
            },
            "quality": quality,
        }

    def test_cross_host_gate_allows_different_toolchain_bytes(self):
        windows = self.baseline("Windows", "1" * 64)
        linux = self.baseline("Linux", "2" * 64)

        comparison = bootstrap_baseline.compare_host_baselines(windows, linux)

        self.assertEqual(comparison["status"], "pass")
        self.assertTrue(comparison["gate"]["passed"])
        self.assertFalse(comparison["artifacts"]["digests_equal"])
        self.assertFalse(comparison["artifacts"]["physical_paths_equal"])
        self.assertEqual(comparison["artifacts"]["logical_artifact_count"], 5)

    def test_cross_host_gate_rejects_incomparable_or_failed_evidence(self):
        windows = self.baseline("Windows", "1" * 64)
        cases = {}

        revision = self.baseline("Linux", "2" * 64)
        revision["source"]["revision"] = "b" * 40
        cases["revision"] = revision

        paths = self.baseline("Linux", "2" * 64)
        paths["builds"][0]["artifacts"]["files"].pop()
        cases["paths"] = paths

        digest = self.baseline("Linux", "2" * 64)
        digest["builds"][0]["artifacts"]["digest"] = "0" * 64
        cases["digest"] = digest

        contradictory = self.baseline("Linux", "2" * 64)
        for build in contradictory["builds"]:
            for entry in build["artifacts"]["files"]:
                if entry["path"] == "kernel/kernel.bin":
                    entry["sha256"] = "4" * 64
            build["artifacts"]["digest"] = bootstrap_baseline._artifact_digest(
                build["artifacts"]["files"]
            )
        cases["root-combined-contradiction"] = contradictory

        run_two = self.baseline("Linux", "2" * 64)
        second_build = run_two["builds"][1]
        for entry in second_build["artifacts"]["files"]:
            if entry["path"] == "kernel/kernel.bin":
                entry["sha256"] = "3" * 64
        for entry in second_build["supported_roots"]["root"]["artifacts"]["files"]:
            if entry["path"] == "kernel/kernel.bin":
                entry["sha256"] = "3" * 64
        second_build["artifacts"]["digest"] = bootstrap_baseline._artifact_digest(
            second_build["artifacts"]["files"]
        )
        second_build["supported_roots"]["root"]["artifacts"][
            "digest"
        ] = bootstrap_baseline._artifact_digest(
            second_build["supported_roots"]["root"]["artifacts"]["files"]
        )
        cases["run-two"] = run_two

        checks = self.baseline("Linux", "2" * 64)
        next(
            check
            for check in checks["builds"][0]["checks"]
            if check["name"] == "user-build"
        )["status"] = "fail"
        cases["checks"] = checks

        reproducibility = self.baseline("Linux", "2" * 64)
        reproducibility["reproducibility"]["matched"] = False
        cases["reproducibility"] = reproducibility

        capability = self.baseline("Linux", "2" * 64)
        capability["tools"]["c_compiler"]["capabilities"][
            "freestanding_i386"
        ]["status"] = "fail"
        cases["capability"] = capability

        old_schema = self.baseline("Linux", "2" * 64)
        old_schema["schema"] = "cupid.bootstrap-baseline.v1"
        cases["old-schema"] = old_schema

        tool_family = self.baseline("Linux", "2" * 64)
        tool_family["tools"]["c_compiler"].update(
            {"executable": "/usr/bin/clang", "version": "clang version 18"}
        )
        cases["tool-family"] = tool_family

        quality = self.baseline("Linux", "2" * 64)
        del quality["quality"]["kernel_text_bytes"]
        cases["quality"] = quality

        quality_artifact = self.baseline("Linux", "2" * 64)
        quality_artifact["quality"]["kernel_binary_bytes"] += 1
        cases["quality-artifact"] = quality_artifact

        fingerprint = self.baseline("Linux", "2" * 64)
        del fingerprint["tools"]["git"]["executable_sha256"]
        cases["tool-fingerprint"] = fingerprint

        for name, linux in cases.items():
            with self.subTest(name=name):
                comparison = bootstrap_baseline.compare_host_baselines(
                    windows, linux
                )
                self.assertEqual(comparison["status"], "fail")
                self.assertFalse(comparison["gate"]["passed"])
                self.assertTrue(comparison["gate"]["failures"])

    def test_cross_host_cli_writes_and_checks_canonical_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            windows = root / "windows.json"
            linux = root / "linux.json"
            output = root / "comparison.json"
            windows.write_text(
                json.dumps(self.baseline("Windows", "1" * 64)),
                encoding="utf-8",
            )
            linux.write_text(
                json.dumps(self.baseline("Linux", "2" * 64)),
                encoding="utf-8",
            )

            write_status = bootstrap_baseline.main(
                [
                    "--compare-hosts",
                    str(windows),
                    str(linux),
                    "--output",
                    str(output),
                ]
            )
            check_status = bootstrap_baseline.main(
                [
                    "--compare-hosts",
                    str(windows),
                    str(linux),
                    "--output",
                    str(output),
                    "--check",
                ]
            )
            output.write_bytes(b"\xff\xfe")
            stale_status = bootstrap_baseline.main(
                [
                    "--compare-hosts",
                    str(windows),
                    str(linux),
                    "--output",
                    str(output),
                    "--check",
                ]
            )

        self.assertEqual(write_status, 0)
        self.assertEqual(check_status, 0)
        self.assertEqual(stale_status, 1)


if __name__ == "__main__":
    unittest.main()

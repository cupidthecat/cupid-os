import ast
import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import artifact_size_policy


REPO_ROOT = Path(__file__).resolve().parents[1]
POLICY_TOOL = REPO_ROOT / "tools/artifact_size_policy.py"
CHECKED_POLICY = REPO_ROOT / "bootstrap/artifact-size-policy.json"
SEED_MANIFEST = REPO_ROOT / "bootstrap/seeds/i386-linux/manifest.json"
WINDOWS_SEED_MANIFEST = (
    REPO_ROOT / "bootstrap/seeds/i386-windows/manifest.json"
)
ARTIFACT_OWNERS = {
    "boot/boot.bin": "CupidASM",
    "bootstrap/seeds/i386-linux/cupidasm.elf": "CupidASM",
    "bootstrap/seeds/i386-linux/cupidc.elf": "CupidC",
    "bootstrap/seeds/i386-linux/cupiddis.elf": "CupidDis",
    "bootstrap/seeds/i386-linux/cupidld.elf": "CupidLD",
    "bootstrap/seeds/i386-linux/cupidobj.elf": "CupidObj",
    "bootstrap/seeds/i386-windows/cupidasm.exe": "CupidASM",
    "bootstrap/seeds/i386-windows/cupidc.exe": "CupidC",
    "bootstrap/seeds/i386-windows/cupiddis.exe": "CupidDis",
    "bootstrap/seeds/i386-windows/cupidld.exe": "CupidLD",
    "bootstrap/seeds/i386-windows/cupidobj.exe": "CupidObj",
    "kernel/kernel.bin": "CupidObj",
    "kernel/kernel.elf": "CupidLD",
    "kernel/kernel.elf.pass1": "CupidLD",
}
SEED_NAMES = ("cupidasm", "cupidc", "cupiddis", "cupidld", "cupidobj")


class ArtifactSizePolicyTests(unittest.TestCase):
    def make_assignment(self, makefile, name):
        lines = makefile.splitlines()
        prefix = f"{name} ="
        for index, line in enumerate(lines):
            if not line.startswith(prefix):
                continue
            value = line[len(prefix) :].strip()
            while value.endswith("\\"):
                value = value[:-1].rstrip()
                index += 1
                value += " " + lines[index].strip()
            return value.split()
        self.fail(f"Makefile assignment is missing: {name}")

    def write_seed_manifest(
        self,
        root,
        sizes,
        directory="bootstrap/seeds/i386-linux",
    ):
        manifest = root / directory / "manifest.json"
        manifest.parent.mkdir(parents=True, exist_ok=True)
        manifest.write_text(
            json.dumps(
                {
                    "artifacts": [
                        {
                            "file": f"{name}.elf",
                            "name": name,
                            "size": sizes[f"{directory}/{name}.elf"],
                        }
                        for name in SEED_NAMES
                    ],
                    "schema": "cupid.bootstrap-seed.v1",
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
            newline="\n",
        )
        return manifest

    def write_fixture(
        self,
        root,
        sizes=None,
        entries=None,
        seed_directory="bootstrap/seeds/i386-linux",
    ):
        owners = {
            path.replace("bootstrap/seeds/i386-linux", seed_directory, 1): owner
            for path, owner in ARTIFACT_OWNERS.items()
        }
        sizes = sizes or {path: index + 1 for index, path in enumerate(owners)}
        for path, size in sizes.items():
            artifact = root / path
            artifact.parent.mkdir(parents=True, exist_ok=True)
            artifact.write_bytes(b"x" * size)
        if entries is None:
            entries = [
                {
                    "exact_bytes": sizes[path],
                    "path": path,
                    "producer": owners[path],
                    "reason": "fixture lock",
                }
                for path in sorted(owners)
            ]
        policy = root / "policy.json"
        policy.write_text(
            json.dumps(
                {
                    "artifacts": entries,
                    "schema": "cupid.artifact-size-policy.v1",
                },
                indent=2,
                sort_keys=True,
            )
            + "\n",
            encoding="utf-8",
            newline="\n",
        )
        manifest = self.write_seed_manifest(
            root,
            sizes,
            directory=seed_directory,
        )
        return policy, manifest, sizes, entries

    def run_policy(self, root, policy, seed_manifest=None):
        if seed_manifest is None:
            seed_manifest = root / "bootstrap/seeds/i386-linux/manifest.json"
        return subprocess.run(
            [
                sys.executable,
                "-B",
                str(POLICY_TOOL),
                "verify",
                "--root",
                str(root),
                "--policy",
                str(policy),
                "--seed-manifest",
                str(seed_manifest),
            ],
            cwd=REPO_ROOT,
            text=True,
            capture_output=True,
            timeout=30,
        )

    def test_verify_accepts_the_complete_exact_output_cohort(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, _, _, _ = self.write_fixture(root)

            result = self.run_policy(root, policy)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "Cupid artifact sizes: ok (14 exact artifacts)\n",
        )
        self.assertEqual(result.stderr, "")

    def test_verify_rejects_missing_duplicate_and_unknown_policy_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            _, _, sizes, entries = self.write_fixture(root)
            cases = {
                "missing": (
                    entries[:-1],
                    "policy is missing required artifacts: kernel/kernel.elf.pass1",
                ),
                "duplicate": (
                    [*entries, dict(entries[0])],
                    "policy artifact is duplicated: boot/boot.bin",
                ),
                "unknown": (
                    [
                        *entries[:-1],
                        {
                            "exact_bytes": 1,
                            "path": "kernel/other.bin",
                            "producer": "CupidObj",
                            "reason": "unknown fixture",
                        },
                    ],
                    "policy has unknown artifacts: kernel/other.bin",
                ),
            }
            for name, (case_entries, diagnostic) in cases.items():
                with self.subTest(name=name):
                    policy, _, _, _ = self.write_fixture(
                        root, sizes=sizes, entries=case_entries
                    )
                    result = self.run_policy(root, policy)
                    self.assertEqual(result.returncode, 1)
                    self.assertEqual(result.stdout, "")
                    self.assertEqual(
                        result.stderr,
                        f"artifact size verification failed: {diagnostic}\n",
                    )

    def test_verify_reports_every_exact_size_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, _, sizes, _ = self.write_fixture(root)
            (root / "boot/boot.bin").write_bytes(b"larger")
            (root / "kernel/kernel.bin").write_bytes(b"")

            result = self.run_policy(root, policy)

        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertEqual(
            result.stderr,
            "artifact size verification failed:\n"
            "- boot/boot.bin has 6 bytes; expected exactly 1 byte\n"
            "- kernel/kernel.bin has 0 bytes; expected exactly "
            f"{sizes['kernel/kernel.bin']} bytes\n",
        )

    def test_verify_rejects_missing_nonregular_and_linked_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, _, _, _ = self.write_fixture(root)
            (root / "kernel/kernel.elf.pass1").unlink()
            (root / "kernel/kernel.elf").unlink()
            (root / "kernel/kernel.elf").mkdir()

            result = self.run_policy(root, policy)

            self.assertEqual(result.returncode, 1)
            self.assertIn(
                "kernel/kernel.elf is not a regular file",
                result.stderr,
            )
            self.assertIn(
                "kernel/kernel.elf.pass1 is missing",
                result.stderr,
            )

            if hasattr(os, "symlink"):
                link = root / "boot/boot.bin"
                target = root / "boot/target.bin"
                link.unlink()
                target.write_bytes(b"x")
                try:
                    link.symlink_to(target)
                except OSError:
                    return
                result = self.run_policy(root, policy)
                self.assertEqual(result.returncode, 1)
                self.assertIn(
                    "boot/boot.bin is linked or reparse-backed",
                    result.stderr,
                )

    def test_verify_rejects_unsafe_and_linked_policy_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, _, _, entries = self.write_fixture(root)
            entries[0]["path"] = "../boot.bin"
            policy.write_text(
                json.dumps(
                    {
                        "artifacts": entries,
                        "schema": "cupid.artifact-size-policy.v1",
                    },
                    indent=2,
                    sort_keys=True,
                )
                + "\n",
                encoding="utf-8",
                newline="\n",
            )

            result = self.run_policy(root, policy)

            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertEqual(
                result.stderr,
                "artifact size verification failed: "
                "policy artifact path is unsafe: ../boot.bin\n",
            )

            real_policy, _, _, _ = self.write_fixture(root)
            linked_policy = root / "linked-policy.json"
            try:
                linked_policy.symlink_to(real_policy)
            except OSError:
                return

            result = self.run_policy(root, linked_policy)

            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, "")
            self.assertEqual(
                result.stderr,
                "artifact size verification failed: policy file "
                "linked-policy.json is linked or reparse-backed\n",
            )

    def test_verify_rejects_a_policy_outside_the_repository(self):
        with tempfile.TemporaryDirectory() as directory:
            container = Path(directory)
            root = container / "repository"
            outside = container / "outside"
            root.mkdir()
            outside.mkdir()
            policy, _, _, _ = self.write_fixture(outside)

            result = self.run_policy(root, policy)

        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertEqual(
            result.stderr,
            "artifact size verification failed: "
            "policy path is outside the repository root\n",
        )

    def test_verify_uses_the_selected_seed_manifest_directory(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, manifest, _, _ = self.write_fixture(
                root,
                seed_directory="bootstrap/seeds/promoted",
            )

            result = self.run_policy(root, policy, manifest)

        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            result.stdout,
            "Cupid artifact sizes: ok (14 exact artifacts)\n",
        )
        self.assertEqual(result.stderr, "")

    def test_verify_rejects_policy_seeds_from_an_unselected_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, _, sizes, _ = self.write_fixture(root)
            promoted_sizes = {}
            for name in SEED_NAMES:
                original = f"bootstrap/seeds/i386-linux/{name}.elf"
                promoted = f"bootstrap/seeds/promoted/{name}.elf"
                promoted_sizes[promoted] = sizes[original]
                path = root / promoted
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"x" * sizes[original])
            selected_manifest = self.write_seed_manifest(
                root,
                promoted_sizes,
                directory="bootstrap/seeds/promoted",
            )

            result = self.run_policy(root, policy, selected_manifest)

        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertIn(
            "policy has unknown artifacts: bootstrap/seeds/i386-linux/cupidasm.elf",
            result.stderr,
        )

    def test_verify_rejects_a_seed_size_that_differs_from_the_manifest(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, manifest, _, _ = self.write_fixture(root)
            decoded = json.loads(manifest.read_text(encoding="utf-8"))
            decoded["artifacts"][0]["size"] += 1
            manifest.write_text(
                json.dumps(decoded, indent=2, sort_keys=True) + "\n",
                encoding="utf-8",
                newline="\n",
            )

            result = self.run_policy(root, policy, manifest)

        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertEqual(
            result.stderr,
            "artifact size verification failed: policy artifact "
            "bootstrap/seeds/i386-linux/cupidasm.elf has exact size 2, "
            "but the selected seed manifest declares 3\n",
        )

    def test_verify_does_not_reopen_checked_paths_by_name(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            policy, manifest, _, _ = self.write_fixture(root)

            with mock.patch.object(
                Path,
                "read_bytes",
                side_effect=AssertionError(
                    "checked paths must be read from pinned handles"
                ),
            ):
                artifact_size_policy.verify(root, policy, manifest)

    def test_final_check_rewalks_each_leaf_from_the_pinned_root(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artifact = root / "artifact.bin"
            artifact.write_bytes(b"stable")
            with artifact_size_policy._PinnedRepository(root) as reader:
                capture, issue = reader.capture(
                    "artifact.bin",
                    read_payload=False,
                )
                self.assertIsNotNone(capture)
                self.assertIsNone(issue)
                original = reader._reopen_file_descriptor
                with mock.patch.object(
                    reader,
                    "_reopen_file_descriptor",
                    wraps=original,
                ) as reopen:
                    reader.require_unchanged()
                reopen.assert_called_once_with(("artifact.bin",))

    def test_windows_walk_opens_every_descendant_from_its_parent_handle(self):
        source = POLICY_TOOL.read_text(encoding="utf-8")
        module = ast.parse(source)
        functions = {
            node.name: node for node in module.body if isinstance(node, ast.FunctionDef)
        }
        repository = next(
            node
            for node in module.body
            if isinstance(node, ast.ClassDef) and node.name == "_PinnedRepository"
        )
        methods = {
            node.name: node
            for node in repository.body
            if isinstance(node, ast.FunctionDef)
        }

        relative_open = functions["_windows_open_relative_handle"]
        nt_open = [
            call
            for call in ast.walk(relative_open)
            if isinstance(call, ast.Call)
            and isinstance(call.func, ast.Attribute)
            and call.func.attr == "NtCreateFile"
        ]
        self.assertEqual(len(nt_open), 1)
        object_attributes = next(
            call
            for call in ast.walk(relative_open)
            if isinstance(call, ast.Call)
            and isinstance(call.func, ast.Name)
            and call.func.id == "attributes_type"
        )
        self.assertEqual(ast.unparse(object_attributes.args[1]), "parent_handle")
        self.assertIn(
            "_WINDOWS_OBJECT_DONT_REPARSE",
            ast.unparse(object_attributes.args[3]),
        )

        for method_name, child_name, directory in (
            ("_pin_directory", "parts[-1]", True),
            ("_open_file_from_parent", "name", False),
            ("_reopen_file_descriptor", "part", True),
        ):
            calls = [
                call
                for call in ast.walk(methods[method_name])
                if isinstance(call, ast.Call)
                and isinstance(call.func, ast.Name)
                and call.func.id == "_windows_open_relative_handle"
            ]
            self.assertEqual(len(calls), 1)
            self.assertEqual(ast.unparse(calls[0].args[0]), "parent")
            self.assertEqual(ast.unparse(calls[0].args[1]), child_name)
            keyword = next(
                item for item in calls[0].keywords if item.arg == "directory"
            )
            self.assertEqual(ast.literal_eval(keyword.value), directory)

    def test_checked_policy_matches_seed_manifest_and_make_target(self):
        policy = json.loads(CHECKED_POLICY.read_text(encoding="utf-8"))
        entries = policy["artifacts"]
        self.assertEqual(
            [entry["path"] for entry in entries],
            sorted(ARTIFACT_OWNERS),
        )
        self.assertEqual(
            {entry["path"]: entry["producer"] for entry in entries},
            ARTIFACT_OWNERS,
        )

        seed_sizes = {}
        for directory, manifest_path in (
            ("bootstrap/seeds/i386-linux/", SEED_MANIFEST),
            ("bootstrap/seeds/i386-windows/", WINDOWS_SEED_MANIFEST),
        ):
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
            seed_sizes.update(
                {
                    directory + artifact["file"]: artifact["size"]
                    for artifact in manifest["artifacts"]
                }
            )
        policy_sizes = {
            entry["path"]: entry["exact_bytes"]
            for entry in entries
            if entry["path"].startswith("bootstrap/seeds/")
        }
        self.assertEqual(policy_sizes, seed_sizes)

        makefile = (REPO_ROOT / "Makefile").read_text(encoding="utf-8")
        make_paths = []
        for token in self.make_assignment(makefile, "ARTIFACT_SIZE_OUTPUTS"):
            if token == "$(BOOTLOADER)":
                make_paths.append("boot/boot.bin")
            elif token == "$(KERNEL)":
                make_paths.append("kernel/kernel.bin")
            elif token.startswith("$(BOOTSTRAP_SEED_DIRECTORY)"):
                make_paths.append(
                    "bootstrap/seeds/i386-linux/"
                    + token.removeprefix("$(BOOTSTRAP_SEED_DIRECTORY)")
                )
            elif token.startswith("$(BOOTSTRAP_WINDOWS_SEED_DIRECTORY)"):
                make_paths.append(
                    "bootstrap/seeds/i386-windows/"
                    + token.removeprefix(
                        "$(BOOTSTRAP_WINDOWS_SEED_DIRECTORY)"
                    )
                )
            else:
                make_paths.append(token)
        self.assertEqual(make_paths, list(ARTIFACT_OWNERS))
        self.assertIn("all: $(OS_IMAGE)", makefile)
        self.assertIn(
            "$(OS_IMAGE): verify-artifact-sizes $(BOOTLOADER) $(KERNEL) \\",
            makefile,
        )
        self.assertIn(
            "verify-artifact-sizes: $(ARTIFACT_SIZE_OUTPUTS) \\",
            makefile,
        )
        target_start = makefile.index("verify-artifact-sizes:")
        target_end = makefile.index("\nbootstrap-from-seed:", target_start)
        artifact_target = makefile[target_start:target_end]
        self.assertNotIn("bootstrap_toolchain.py verify", artifact_target)
        self.assertIn(
            "tools/artifact_size_contract.py verify --root . \\",
            makefile,
        )
        self.assertIn(
            "--policy $(ARTIFACT_SIZE_POLICY)",
            makefile,
        )
        self.assertIn(
            "--seed-manifest $(BOOTSTRAP_SEED_MANIFEST)",
            makefile,
        )
        self.assertIn(
            "--checked-manifest $(BOOTSTRAP_WINDOWS_SEED_MANIFEST)",
            makefile,
        )
        self.assertIn(
            "--execution-manifest $(PRODUCTION_SEED_MANIFEST)",
            makefile,
        )


if __name__ == "__main__":
    unittest.main()

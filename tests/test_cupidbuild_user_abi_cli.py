"""The public ABI command, built from fresh objects by the installed Cupid tools."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tools import artifact_size_contract as contract
from tools import bootstrap_toolchain as bootstrap
from tools import user_syscall_abi as oracle


ROOT = Path(__file__).resolve().parents[1]


class CupidBuiltUserAbiCommandTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="cupid-user-abi-command-")
        cls.addClassCleanup(cls.build.cleanup)
        folder = Path(cls.build.name)
        cls.program = folder / ("cupidbuild.exe" if os.name == "nt" else "cupidbuild.elf")
        source = folder / "source"
        installed = json.loads((ROOT / "bootstrap/seeds/i386-linux/manifest.json").read_text())["build_plan"]
        plan = bootstrap._candidate_build_plan(installed)
        if os.name == "nt":
            plan = bootstrap._windows_build_plan(plan, utf8=True, user_link_aliases=True)
        order = plan["links"]["cupidbuild"]
        rows = [row for row in plan["sources"] if row["name"] in order]
        assembly = [row for row in plan.get("assembly_sources", [
            {"name": "start", "path": "/toolchain/hosted/i386-linux/start.asm"}
        ]) if row["name"] in order]
        paths = {ROOT / row["path"].lstrip("/") for row in (*rows, *assembly)}
        paths.update((ROOT / "toolchain").glob("*.h"))
        paths.update(path for path in (ROOT / "toolchain/hosted").rglob("*") if path.is_file())
        captures = {path: path.read_bytes() for path in paths}
        for path, payload in captures.items():
            destination = source / path.relative_to(ROOT)
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(payload)
        host = "i386-windows" if os.name == "nt" else "i386-linux"
        checked = bootstrap.freeze_seed_inputs(ROOT / "bootstrap/seeds" / host / "manifest.json", folder / "seed")
        cls.addClassCleanup(bootstrap.require_live_seed_inputs, checked)
        runner = bootstrap.ToolRunner(source)
        objects = {}
        for row in rows:
            output = source / (row["name"] + ".o")
            contract._compile_source(checked, runner, source, row["path"].lstrip("/"), output,
                                     row.get("definitions", ()), row["gnu_extensions"], 600)
            objects[row["name"]] = output
            bootstrap._validate_i386_relocatable(output)
        for row in assembly:
            output = source / (row["name"] + ".o")
            contract._run_checked_tool(checked, runner, "cupidasm",
                ["-f", "elf32", source / row["path"].lstrip("/"), "-o", output], row["name"], 180)
            objects[row["name"]] = output
            bootstrap._validate_i386_relocatable(output)
        if os.name == "nt":
            arguments = bootstrap._windows_link_arguments("cupidbuild", cls.program, objects, order,
                                                          utf8=True, user_link_aliases=True)
            contract._run_checked_tool(checked, runner, "cupidld", arguments, "ABI command", 180)
            bootstrap._validate_static_i386_pe32(
                cls.program, int(bootstrap.EXPECTED_WINDOWS_TARGET["entry"]),
                bootstrap._windows_utf8_imports("cupidbuild", user_link_aliases=True))
        else:
            contract._link_contract(checked, runner, [objects[name] for name in order], cls.program, False, 180)
            cls.program.chmod(0o700)
        for path, payload in captures.items():
            if path.read_bytes() != payload or (source / path.relative_to(ROOT)).read_bytes() != payload:
                raise AssertionError(f"checked ABI command source changed: {path}")
        bootstrap.require_live_seed_inputs(checked)
        cls.program_sha256 = hashlib.sha256(cls.program.read_bytes()).hexdigest()

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="cupid-abi-inputs-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        for path in oracle.ABI_INPUTS:
            destination = self.root / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / path, destination)

    def run_command(self, *arguments, cwd=None):
        result = subprocess.run([str(self.program), *arguments], cwd=cwd,
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(hashlib.sha256(self.program.read_bytes()).hexdigest(), self.program_sha256)
        return result

    def test_checked_command_matches_every_oracle_field(self):
        result = self.run_command("verify-user-abi", "--root", str(self.root))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        self.assertEqual(json.loads(result.stdout), oracle.check_syscall_abi(self.root))

    def test_checked_command_passes_the_complete_staged_behavior_gate(self):
        stage = bootstrap.Stage({}, {"cupidbuild": self.program})
        behavior = self.root / "behavior"
        behavior.mkdir()
        bootstrap._check_cupidbuild_user_abi_behavior(
            bootstrap.ToolRunner(self.root), ROOT, behavior, stage, stage, "checked ")
        record = json.loads((behavior / "cupidbuild-user-abi/behavior.json").read_text())
        self.assertEqual(len(record["cases"]), 22)
        self.assertEqual(sum(row["status"] == 0 for row in record["cases"]), 11)
        self.assertEqual(record["oracle_report"], oracle.check_syscall_abi(ROOT))
        self.assertEqual(record["tool_sha256"], [self.program_sha256] * 2)

    def test_checked_command_resolves_relative_roots(self):
        for root in (".", "./", "././", str(self.root) + "/./"):
            with self.subTest(root=root):
                result = self.run_command("verify-user-abi", "--root", root, cwd=self.root)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(json.loads(result.stdout), oracle.check_syscall_abi(self.root))

    @unittest.skipIf(os.name == "nt", "POSIX retained roots reject parent components")
    def test_checked_command_rejects_parent_components_without_hiding_ancestors(self):
        before = self.run_command("verify-user-abi", "--root", str(self.root)).stdout
        result = self.run_command("verify-user-abi", "--root", str(self.root / "missing" / ".."))
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertFalse((self.root / "missing").exists())
        self.assertEqual(self.run_command("verify-user-abi", "--root", str(self.root)).stdout, before)

    def test_checked_command_rejects_semantic_drift_and_recovers(self):
        path = self.root / "user/cupid.h"
        before = path.read_bytes()
        path.write_bytes(before.replace(b"#define CUPID_SYSCALL_VERSION 5", b"#define CUPID_SYSCALL_VERSION 4"))
        result = self.run_command("verify-user-abi", "--root", str(self.root))
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertIn("syscall version differs", result.stderr)
        path.write_bytes(before)
        self.assertEqual(self.run_command("verify-user-abi", "--root", str(self.root)).returncode, 0)

    def test_checked_command_rejects_incomplete_literals_and_recovers(self):
        path = self.root / "kernel/core/types.h"
        before = path.read_bytes()
        for tail in (b' "', b" '", b' "\\"', b" '\\'"):
            with self.subTest(tail=tail):
                path.write_bytes(before + tail)
                result = self.run_command("verify-user-abi", "--root", str(self.root))
                self.assertEqual(result.returncode, 1)
                self.assertEqual(result.stdout, "")
                self.assertIn("incomplete string or character literal", result.stderr)
                path.write_bytes(before)
                self.assertEqual(self.run_command("verify-user-abi", "--root", str(self.root)).returncode, 0)

    def test_checked_command_rejects_missing_inputs(self):
        (self.root / "kernel/core/syscall.cc").unlink()
        result = self.run_command("verify-user-abi", "--root", str(self.root))
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, "")
        self.assertIn("ABI verification failed", result.stderr)

    def test_checked_command_rejects_bad_arguments_before_work(self):
        for arguments in (("verify-user-abi",), ("verify-user-abi", "--root"),
                          ("verify-user-abi", "--root", ""),
                          ("verify-user-abi", "--root", str(self.root), "--root", str(self.root)),
                          ("verify-user-abi", "--root", str(self.root), "--seed-manifest", "manifest.json")):
            with self.subTest(arguments=arguments):
                result = self.run_command(*arguments)
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, "")
                self.assertIn("verify-user-abi --root ROOT", result.stderr)

    def test_checked_command_accepts_unicode_repository_paths(self):
        target = self.root / "café-日本語-😀"
        target.mkdir()
        for name in ("kernel", "user"):
            shutil.move(str(self.root / name), str(target / name))
        result = self.run_command("verify-user-abi", "--root", str(target))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout), oracle.check_syscall_abi(target))


if __name__ == "__main__":
    unittest.main()

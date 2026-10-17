import ast
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

from tools import bootstrap_toolchain as seed
from tools import bootstrap_user_abi as gate
from tools import build_graph_audit as audit
from tools import user_syscall_abi as oracle


ROOT = Path(__file__).resolve().parents[1]


class StandaloneBootstrapImportTests(unittest.TestCase):
    def run_gate_import(self, local_helper):
        with tempfile.TemporaryDirectory(prefix="cupid-bootstrap-import-") as temporary:
            directory = Path(temporary)
            root = directory / "local"
            foreign = directory / "foreign"
            for parent in (root, foreign):
                (parent / "tools").mkdir(parents=True)
                (parent / "tools/__init__.py").write_bytes(b"")
            shutil.copyfile(ROOT / "tools/bootstrap_toolchain.py",
                            root / "tools/bootstrap_toolchain.py")
            (foreign / "tools/bootstrap_user_abi.py").write_text(
                "def check_behavior(*args):\n    print('foreign-helper')\n", encoding="utf-8")
            (foreign / "tools/bootstrap_toolchain.py").write_text(
                "class BootstrapError(Exception):\n    pass\n", encoding="utf-8")
            if local_helper:
                (root / "tools/bootstrap_user_abi.py").write_text(
                    "def check_behavior(*args):\n    print('local-helper')\n", encoding="utf-8")
            code = (
                "import runpy, sys; sys.path.insert(0, sys.argv[2]); "
                "driver = runpy.run_path(sys.argv[1]); "
                "driver['_check_cupidbuild_user_abi_behavior'](None, None, None, None, None, 'probe ')"
            )
            return subprocess.run([sys.executable, "-I", "-c", code,
                str(root / "tools/bootstrap_toolchain.py"), str(foreign)],
                cwd=directory, capture_output=True, text=True, timeout=30)

    def test_standalone_bootstrap_resolves_its_local_gate_from_unrelated_cwd(self):
        result = self.run_gate_import(True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "local-helper\n")
        self.assertEqual(result.stderr, "")

    def test_missing_local_gate_cannot_fall_back_to_foreign_package(self):
        result = self.run_gate_import(False)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(result.stdout, "")
        self.assertIn("No module named 'tools.bootstrap_user_abi'", result.stderr)


class AbiRunner:
    def __init__(self, source, stages, defect=None):
        self.source, self.stages, self.defect = source, stages, defect
        self.report = oracle.check_syscall_abi(source)
        self.calls = []

    def run(self, executable, arguments, timeout):
        pair = len(self.calls) // 2
        generation = len(self.calls) % 2
        self.calls.append((executable, arguments, timeout))
        diagnostics = {2: "syscall version differs", 4: "syscall provider contract changed",
                       6: "incomplete string or character literal",
                       8: "incomplete string or character literal",
                       10: "incomplete string or character literal",
                       12: "incomplete string or character literal",
                       14: "NUL-free UTF-8", 16: "ABI verification failed"}
        status = 2 if pair in (18, 19, 20) else 1 if pair in diagnostics else 0
        stdout = json.dumps(self.report) if status == 0 else ""
        stderr = diagnostics.get(pair, "verify-user-abi --root ROOT" if status == 2 else "")
        if self.defect == "stages differ" and generation:
            stdout += " "
        if pair == 0:
            if self.defect == "oracle field":
                stdout = stdout.replace('"table_size": 412', '"table_size": 411')
            if self.defect == "float":
                stdout = stdout.replace('"version": 5', '"version": 5.0')
            if self.defect == "boolean":
                stdout = stdout.replace('"version": 5', '"version": true')
            if self.defect == "duplicate":
                stdout = stdout[:-1] + ', "version": 5}'
            if self.defect == "nan":
                stdout = stdout.replace('"version": 5', '"version": NaN')
            if self.defect == "missing field":
                report = dict(self.report)
                del report["scalar_types"]
                stdout = json.dumps(report)
            if self.defect == "success stderr":
                stderr = "unexpected diagnostic"
            if self.defect == "fixture bytes":
                path = arguments[2] / "kernel/core/types.h"
                path.write_bytes(path.read_bytes() + b"\n")
            if self.defect == "fixture timestamp":
                path = arguments[2] / "kernel/core/types.h"
                observed = path.stat()
                os.utime(path, ns=(observed.st_atime_ns, observed.st_mtime_ns + 1_000_000_000))
            if self.defect == "fixture lock":
                (arguments[2] / ".cupidbuild.lock").write_bytes(b"lock")
            if self.defect == "original bytes":
                path = self.source / "kernel/core/types.h"
                path.write_bytes(path.read_bytes() + b"\n")
            if self.defect == "tool bytes":
                executable.write_bytes(b"changed tool")
        if pair == 2:
            if self.defect == "failure stdout":
                stdout = json.dumps(self.report)
            if self.defect == "missing diagnostic":
                stderr = ""
            if self.defect == "wrong diagnostic":
                stderr = "unrelated error"
            if self.defect == "accepted version":
                status = 0
        if pair == 21 and self.defect == "recovery":
            stdout = stdout.replace('"version": 5', '"version": 4')
        return subprocess.CompletedProcess([], status, stdout, stderr)


class UserAbiBehaviorTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="cupid-abi-gate-")
        self.addCleanup(self.directory.cleanup)
        self.source, self.behavior, self.stages = self.fixture(Path(self.directory.name))

    @staticmethod
    def fixture(root):
        source = root / "source"
        for name in oracle.ABI_INPUTS:
            destination = source / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, destination)
        behavior = root / "behavior"
        behavior.mkdir()
        stages = []
        for name in ("stage-three", "stage-four"):
            program = root / name / "cupidbuild"
            program.parent.mkdir()
            program.write_bytes(b"retained fake tool " + name.encode("ascii"))
            stages.append(seed.Stage({}, {"cupidbuild": program}))
        return source, behavior, stages

    def run_gate(self, defect=None):
        runner = AbiRunner(self.source, self.stages, defect)
        seed._check_cupidbuild_user_abi_behavior(
            runner, self.source, self.behavior, *self.stages, "test ")
        return runner

    def test_retains_complete_oracle_inputs_and_twenty_two_command_pairs(self):
        runner = self.run_gate()
        self.assertEqual(len(runner.calls), 44)
        self.assertTrue(all(call[2] == 60 for call in runner.calls))
        for first, second in zip(runner.calls[::2], runner.calls[1::2]):
            self.assertEqual(first[1], second[1])
            self.assertEqual(first[0], self.stages[0].tools["cupidbuild"])
            self.assertEqual(second[0], self.stages[1].tools["cupidbuild"])
        record = json.loads((self.behavior / "cupidbuild-user-abi/behavior.json").read_text())
        self.assertEqual(record["oracle_report"], oracle.check_syscall_abi(self.source))
        self.assertEqual(set(record["source_inputs"]), set(oracle.ABI_INPUTS))
        self.assertEqual([row["status"] for row in record["cases"]].count(0), 11)
        self.assertEqual([row["status"] for row in record["cases"]].count(1), 8)
        self.assertEqual([row["status"] for row in record["cases"]].count(2), 3)

    def test_rejects_output_mutation_generation_and_recovery_regressions(self):
        for defect, message in (
            ("oracle field", "complete oracle report differs"),
            ("float", "JSON numbers must be integers"),
            ("boolean", "complete oracle report differs"),
            ("duplicate", "duplicate decoded JSON key"),
            ("nan", "JSON numbers must be integers"),
            ("missing field", "complete oracle report differs"),
            ("success stderr", "complete oracle report differs"),
            ("fixture bytes", "changed its input tree"),
            ("fixture timestamp", "changed its input tree"),
            ("fixture lock", "changed its input tree"),
            ("original bytes", "original ABI inputs changed"),
            ("tool bytes", "staged CupidBuild changed"),
            ("stages differ", "behavior differs across stages"),
            ("failure stdout", "failure output differs"),
            ("missing diagnostic", "failure output differs"),
            ("wrong diagnostic", "failure output differs"),
            ("accepted version", "returned 0, expected 1"),
            ("recovery", "complete oracle report differs"),
        ):
            with self.subTest(defect=defect):
                # Each defect runs from a fresh capture and must publish no success evidence.
                with tempfile.TemporaryDirectory(prefix="cupid-abi-defect-") as temporary:
                    source, behavior, stages = self.fixture(Path(temporary))
                    runner = AbiRunner(source, stages, defect)
                    with self.assertRaisesRegex(seed.BootstrapError, message):
                        seed._check_cupidbuild_user_abi_behavior(
                            runner, source, behavior, *stages, "test ")
                    self.assertFalse((behavior / "cupidbuild-user-abi/behavior.json").exists())

    def test_missing_or_oversized_input_denies_commands_and_evidence(self):
        for payload in (None, b"x" * (gate.MAX_INPUT_BYTES + 1)):
            with self.subTest(missing=payload is None):
                path = self.source / "kernel/core/types.h"
                before = path.read_bytes()
                if payload is None:
                    path.unlink()
                else:
                    path.write_bytes(payload)
                runner = mock.Mock()
                with self.assertRaises(seed.BootstrapError):
                    gate.check_behavior(runner, self.source, self.behavior, *self.stages, "test ")
                runner.run.assert_not_called()
                self.assertFalse((self.behavior / "cupidbuild-user-abi").exists())
                path.write_bytes(before)

    def test_both_matrices_pass_the_actual_profile_source_and_final_generations(self):
        tree = ast.parse(Path(seed.__file__).read_text(encoding="utf-8"))
        for name in ("_run_behavior_checks", "_run_native_windows_behavior_checks"):
            function = next(node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name == name)
            calls = [node for node in ast.walk(function) if isinstance(node, ast.Call)
                     and isinstance(node.func, ast.Name) and node.func.id == "_check_cupidbuild_user_abi_behavior"]
            self.assertEqual(len(calls), 1, name)
            self.assertEqual([node.id for node in calls[0].args[:5]],
                             ["runner", "profile_source_root", "behavior_root", "stage_two", "stage_three"])

    def test_existing_behavior_evidence_is_preserved_without_running_commands(self):
        occupied = self.behavior / "cupidbuild-user-abi"
        occupied.mkdir()
        evidence = occupied / "behavior.json"
        evidence.write_bytes(b"prior retained evidence")
        runner = mock.Mock()
        with self.assertRaises(seed.BootstrapError):
            gate.check_behavior(runner, self.source, self.behavior, *self.stages, "test ")
        runner.run.assert_not_called()
        self.assertEqual(evidence.read_bytes(), b"prior retained evidence")

    def test_audit_rejects_lost_gate_oracle_failure_and_input_checks(self):
        driver = ROOT / "tools/bootstrap_toolchain.py"
        module = ROOT / "tools/bootstrap_user_abi.py"
        original_read = Path.read_text
        sources = {path: original_read(path, encoding="utf-8") for path in (driver, module)}
        contract = audit._cupid_toolchain_fixed_point_contract(ROOT)
        self.assertEqual(contract["success_behavior_cases"], 73)
        self.assertEqual(contract["windows_success_behavior_cases"], 60)
        self.assertEqual(contract["user_abi_behavior_sha256"], audit._source_digest(module))
        mutations = (
            (driver, 'runner, profile_source_root, behavior_root, stage_two, stage_three, "",',
             'runner, source_root, behavior_root, stage_two, stage_three, "",'),
            (driver, '_check_cupidbuild_user_abi_behavior(\n'
             '        runner, profile_source_root, behavior_root, stage_two, stage_three, "native Windows ",\n'
             '    )', 'pass'),
            (module, '_canonical(actual) != _canonical(expected)', 'False'),
            (module, 'result.stdout or not result.stderr or diagnostic not in result.stderr', 'False'),
            (module, '_tree(root) != before', 'False'),
            (module, '_capture(source_root, oracle.ABI_INPUTS) != original', 'False'),
        )
        for path, old, new in mutations:
            with self.subTest(fragment=old):
                self.assertEqual(sources[path].count(old), 1)
                changed = sources[path].replace(old, new, 1)
                def read_text(candidate, *args, **kwargs):
                    return changed if candidate == path else original_read(candidate, *args, **kwargs)
                with mock.patch.object(Path, "read_text", read_text), self.assertRaisesRegex(
                        audit.AuditError, "fixed-point user ABI behavior differs"):
                    audit._cupid_toolchain_fixed_point_contract(ROOT)


if __name__ == "__main__":
    unittest.main()

import ast
import os
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import bootstrap_toolchain as bootstrap
from tools import build_graph_audit
from tests.test_bootstrap_compile_kernel_behavior import object_bytes


class DoomRunner:
    def __init__(self, defect=None):
        self.defect = defect
        self.calls = []

    def run(self, executable, arguments, timeout):
        self.calls.append((executable, arguments, timeout))
        root = arguments[arguments.index("--root") + 1]
        manifest = arguments[arguments.index("--seed-manifest") + 1]
        if not manifest.is_relative_to(root):
            raise AssertionError("manifest must belong to its stage root")
        source = arguments[arguments.index("--source") + 1]
        output = root / arguments[arguments.index("--output") + 1]
        text = (root / source).read_text()
        diagnostic = ""
        if "broken" in text or "absent-stage-doom.h" in text:
            diagnostic = "checked CupidC failed"
        elif not (root / "kernel/doom/dglibc_compat.h").exists():
            diagnostic = "checked CupidC failed"
        elif (not (root / "kernel/doom/src/info.cc").exists()
              or (root / "kernel/doom/legacy.c").exists()):
            diagnostic = "approved source cohort differs"
        if diagnostic:
            if self.defect == "failure bytes":
                output.write_bytes(b"overwritten")
            if self.defect == "failure timestamp":
                stat = output.stat()
                os.utime(output, ns=(stat.st_atime_ns, stat.st_mtime_ns + 1_000_000_000))
            if self.defect == "missing diagnostic":
                diagnostic = ""
            if self.defect == "accept missing include" and "absent-stage-doom.h" in text:
                return subprocess.CompletedProcess([], 0, "", "")
            return subprocess.CompletedProcess([], 1, "", diagnostic)
        payload = object_bytes(True) + (b"tree" if "/src/" in source else b"compat")
        if self.defect == "different stages" and executable.parent.name == "stage-four":
            payload += b"different"
        if self.defect == "recovery" and len(self.calls) > 16:
            payload += b"different"
        if self.defect == "invalid object":
            payload = b"invalid ELF"
        if not output.exists() or output.read_bytes() != payload or self.defect == "replay timestamp":
            output.write_bytes(payload)
        if self.defect == "cleanup":
            output.with_name(output.name + ".cupidbuild.lock").write_text("retained")
        return subprocess.CompletedProcess([], 0, "", "")


class CompileDoomBehaviorTests(unittest.TestCase):
    def run_behavior(self, runner):
        with tempfile.TemporaryDirectory(prefix="cupid-doom-behavior-") as temporary:
            root = Path(temporary)
            stages = tuple(bootstrap.Stage({}, {name: Path(stage) / name
                           for name in bootstrap.CANDIDATE_TOOL_NAMES})
                           for stage in ("stage-three", "stage-four"))
            with mock.patch.object(bootstrap, "_materialize_behavior_seed",
                    side_effect=lambda inputs, directory, name, stage:
                    directory / name / "manifest.json") as materialize, mock.patch.object(
                    bootstrap, "_materialize_cupidbuild_profile_behavior_root") as profiles:
                bootstrap._check_cupidbuild_compile_doom_behavior(
                    runner, mock.sentinel.source_root, root, *stages,
                    mock.sentinel.seed_inputs, "test ")
                self.assertEqual(materialize.call_count, 2)
                self.assertEqual(profiles.call_count, 2)
                for index, call in enumerate(materialize.call_args_list):
                    self.assertEqual(call.args[0], mock.sentinel.seed_inputs)
                    self.assertEqual(call.args[2], "seed")
                    self.assertIs(call.args[3], stages[index])
                    self.assertEqual(profiles.call_args_list[index].args,
                                     (mock.sentinel.source_root, call.args[1]))

    def test_compares_profiles_replay_failures_and_recovery(self):
        runner = DoomRunner()
        self.run_behavior(runner)
        self.assertEqual(len(runner.calls), 20)
        self.assertTrue(all(timeout == 190 for _, _, timeout in runner.calls))
        for first, second in zip(runner.calls[::2], runner.calls[1::2]):
            self.assertEqual(first[0].parent.name, "stage-three")
            self.assertEqual(second[0].parent.name, "stage-four")
            self.assertNotEqual(first[1][4], second[1][4])

    def test_rejects_behavior_regressions(self):
        for defect, message in (
            ("failure bytes", "failure preservation differs"),
            ("failure timestamp", "failure preservation differs"),
            ("missing diagnostic", "failure preservation differs"),
            ("accept missing include", "returned 0, expected 1"),
            ("different stages", "output differs"),
            ("recovery", "output differs"),
            ("replay timestamp", "rewrote an unchanged object"),
            ("invalid object", "not little-endian ELF32"),
            ("cleanup", "left transaction files"),
        ):
            with self.subTest(defect=defect), self.assertRaisesRegex(bootstrap.BootstrapError, message):
                self.run_behavior(DoomRunner(defect))

    def test_linux_and_windows_matrices_call_the_same_gate_once(self):
        tree = ast.parse(Path(bootstrap.__file__).read_text(encoding="utf-8"))
        for name in ("_run_behavior_checks", "_run_native_windows_behavior_checks"):
            function = next(node for node in tree.body
                            if isinstance(node, ast.FunctionDef) and node.name == name)
            calls = [node for node in ast.walk(function) if isinstance(node, ast.Call)
                     and isinstance(node.func, ast.Name)
                     and node.func.id == "_check_cupidbuild_compile_doom_behavior"]
            self.assertEqual(len(calls), 1, name)
            self.assertEqual(calls[0].args[1].id, "profile_source_root")

    def test_audit_rejects_lost_preservation_recovery_and_stage_binding(self):
        root = Path(__file__).resolve().parents[1]
        path = root / "tools/bootstrap_toolchain.py"
        original_read = Path.read_text
        original = original_read(path, encoding="utf-8")
        contract = build_graph_audit._cupid_toolchain_fixed_point_contract(root)
        self.assertEqual(contract["success_behavior_cases"], 54)
        self.assertEqual(contract["windows_success_behavior_cases"], 41)
        for old, new in (
            ("tuple(output.stat().st_mtime_ns for output in outputs) != timestamps", "False"),
            ("success(tree_source, expected_tree)", "success(tree_source)"),
            ('_materialize_behavior_seed(seed_inputs, root, "seed", stage)',
             '_materialize_behavior_seed(seed_inputs, root, "seed", stage_two)'),
        ):
            with self.subTest(fragment=old):
                self.assertEqual(original.count(old), 1)
                changed = original.replace(old, new, 1)
                def read_text(candidate, *args, **kwargs):
                    return changed if candidate == path else original_read(candidate, *args, **kwargs)
                with mock.patch.object(Path, "read_text", read_text), self.assertRaisesRegex(
                        build_graph_audit.AuditError, "fixed-point Doom compile behavior differs"):
                    build_graph_audit._cupid_toolchain_fixed_point_contract(root)


if __name__ == "__main__":
    unittest.main()

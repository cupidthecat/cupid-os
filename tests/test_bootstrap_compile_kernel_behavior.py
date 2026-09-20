import ast
import os
import struct
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tools import bootstrap_toolchain as bootstrap
from tools import build_graph_audit


def object_bytes(has_code):
    contents = bytearray(93)
    contents[:7] = b"\x7fELF\x01\x01\x01"
    struct.pack_into("<HHI", contents, 16, 1, 3, 1)
    struct.pack_into("<I", contents, 32, 52)
    struct.pack_into("<HH", contents, 46, 40, 1)
    struct.pack_into("<IIIIII", contents, 52, 0, 1, 6 if has_code else 2, 0, 92, 1)
    return bytes(contents)


class CompileRunner:
    def __init__(self, defect=None):
        self.defect = defect
        self.calls = []

    def run(self, executable, arguments, timeout):
        self.calls.append((executable, arguments, timeout))
        root = arguments[arguments.index("--root") + 1]
        manifest = arguments[arguments.index("--seed-manifest") + 1]
        if not manifest.is_relative_to(root):
            raise AssertionError("manifest must be inside its compiler root")
        source = arguments[arguments.index("--source") + 1]
        output = root / arguments[arguments.index("--output") + 1]
        text = (root / source).read_text()
        diagnostic = ""
        if "broken" in text or "live-only.h" in text:
            diagnostic = "cupidbuild: checked CupidC failed\n"
        elif not (root / "kernel/core/types.h").exists():
            diagnostic = "cupidbuild: closure cannot be captured\n"
        if diagnostic:
            if self.defect == "failure bytes":
                output.write_bytes(b"overwritten")
            if self.defect == "failure timestamp":
                stat = output.stat()
                os.utime(output, ns=(stat.st_atime_ns, stat.st_mtime_ns + 1_000_000_000))
            if self.defect == "diagnostic":
                diagnostic = "unexpected failure\n"
            if self.defect == "live fallback" and "live-only.h" in text:
                output.write_bytes(object_bytes(False))
                return subprocess.CompletedProcess([], 0, "", "")
            return subprocess.CompletedProcess([], 1, "", diagnostic)
        has_code = source == "kernel/core/string.cc"
        if self.defect == "data code" and not has_code:
            has_code = True
        payload = object_bytes(has_code)
        if self.defect == "different stages" and executable.parent.name == "stage-four":
            payload += b"different"
        if self.defect == "recovery" and len(self.calls) > 12:
            payload += b"different"
        if self.defect == "invalid object":
            payload = b"invalid ELF"
        if not output.exists() or output.read_bytes() != payload or self.defect == "replay timestamp":
            output.write_bytes(payload)
        if self.defect == "cleanup":
            output.with_name(output.name + ".cupidbuild.lock").write_text("retained")
        return subprocess.CompletedProcess([], 0, "", "")


class CompileKernelBehaviorTests(unittest.TestCase):
    def run_behavior(self, runner):
        with tempfile.TemporaryDirectory(prefix="cupid-compile-behavior-") as temporary:
            root = Path(temporary)
            stages = (
                bootstrap.Stage({}, {name: Path("stage-three") / name
                                     for name in bootstrap.CANDIDATE_TOOL_NAMES}),
                bootstrap.Stage({}, {name: Path("stage-four") / name
                                     for name in bootstrap.CANDIDATE_TOOL_NAMES}),
            )
            with mock.patch.object(
                bootstrap, "_materialize_behavior_seed",
                side_effect=lambda inputs, directory, name, stage:
                    directory / name / "manifest.json",
            ) as materialize:
                bootstrap._check_cupidbuild_compile_kernel_behavior(
                    runner, root, *stages,
                    mock.sentinel.seed_inputs, "test ",
                )
                self.assertEqual(materialize.call_count, 2)
                for index, call in enumerate(materialize.call_args_list):
                    self.assertEqual(call.args[0], mock.sentinel.seed_inputs)
                    self.assertEqual(call.args[1], root / "cupidbuild-compile-kernel" /
                                     ("stage-three-root" if index == 0 else "stage-four-root"))
                    self.assertEqual(call.args[2], "seed")
                    self.assertIs(call.args[3], stages[index])

    def test_compares_both_generations_for_success_failures_and_recovery(self):
        runner = CompileRunner()
        self.run_behavior(runner)
        self.assertEqual(len(runner.calls), 14)
        self.assertEqual([timeout for _, _, timeout in runner.calls],
                         [190, 190, 610, 610, 190, 190, 190, 190, 190, 190, 190, 190, 190, 190])
        for first, second in zip(runner.calls[::2], runner.calls[1::2]):
            self.assertEqual(first[0].parent.name, "stage-three")
            self.assertEqual(second[0].parent.name, "stage-four")
            self.assertNotEqual(first[1][4], second[1][4])

    def test_rejects_behavior_regressions(self):
        for defect, message in (
            ("failure bytes", "failure preservation differs"),
            ("failure timestamp", "failure preservation differs"),
            ("diagnostic", "failure preservation differs"),
            ("live fallback", "returned 0, expected 1"),
            ("data code", "code/data policy differs"),
            ("different stages", "output differs"),
            ("recovery", "output differs"),
            ("replay timestamp", "rewrote an unchanged object"),
            ("invalid object", "not little-endian ELF32"),
            ("cleanup", "left transaction files"),
        ):
            with self.subTest(defect=defect), self.assertRaisesRegex(
                bootstrap.BootstrapError, message
            ):
                self.run_behavior(CompileRunner(defect))

    def test_linux_and_windows_matrices_call_the_same_gate_once(self):
        tree = ast.parse(Path(bootstrap.__file__).read_text(encoding="utf-8"))
        for name in ("_run_behavior_checks", "_run_native_windows_behavior_checks"):
            function = next(node for node in tree.body
                            if isinstance(node, ast.FunctionDef) and node.name == name)
            calls = [node for node in ast.walk(function) if isinstance(node, ast.Call)
                     and isinstance(node.func, ast.Name)
                     and node.func.id == "_check_cupidbuild_compile_kernel_behavior"]
            self.assertEqual(len(calls), 1, name)

    def test_audit_rejects_lost_preservation_recovery_and_seed_binding(self):
        root = Path(__file__).resolve().parents[1]
        bootstrap_path = root / "tools/bootstrap_toolchain.py"
        original_read = Path.read_text
        original = original_read(bootstrap_path, encoding="utf-8")
        contract = build_graph_audit._cupid_toolchain_fixed_point_contract(root)
        self.assertEqual(contract["success_behavior_cases"], 42)
        self.assertEqual(contract["windows_success_behavior_cases"], 29)
        for old, new in (
            ("tuple(output.stat().st_mtime_ns for output in outputs) != before", "False"),
            ("success(code_source, expected=expected_code)", "success(code_source)"),
            ('_materialize_behavior_seed(seed_inputs, roots[1], "seed", stage_three)',
             '_materialize_behavior_seed(seed_inputs, roots[1], "seed", stage_two)'),
        ):
            with self.subTest(fragment=old):
                self.assertEqual(original.count(old), 1)
                changed = original.replace(old, new, 1)

                def read_text(path, *args, **kwargs):
                    return changed if path == bootstrap_path else original_read(path, *args, **kwargs)

                with mock.patch.object(Path, "read_text", read_text), self.assertRaisesRegex(
                    build_graph_audit.AuditError, "fixed-point kernel compile behavior differs"
                ):
                    build_graph_audit._cupid_toolchain_fixed_point_contract(root)


if __name__ == "__main__":
    unittest.main()

"""Public native artifact-verifier behavior, compared with the Python oracle."""
import ctypes
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from tools import artifact_size_policy as oracle

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ("cupidbuild_artifacts", "artifact_size_policy", "contract_parse_internal",
           "seed_manifest", "seed_release", "cupidbuild", "cupidbuild_host",
           "ctool", "ctool_host", "elf32", "path_encoding")

class Request(ctypes.Structure):
    _fields_ = [(name, ctypes.c_char_p) for name in ("root", "policy", "manifest")]

class Result(ctypes.Structure):
    _fields_ = [("count", ctypes.c_uint32), ("bytes", ctypes.c_uint64)]

class CupidBuildArtifactTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="artifact-native-")
        cls.addClassCleanup(cls.build.cleanup)
        folder = Path(cls.build.name)
        compiler = shutil.which("clang" if os.name == "nt" else "cc")
        cls.cli = folder / ("cupidbuild.exe" if os.name == "nt" else "cupidbuild")
        library = folder / ("artifacts.dll" if os.name == "nt" else "artifacts.so")
        common = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                  "-D_CRT_SECURE_NO_WARNINGS", "-I", str(ROOT / "toolchain"), "-x", "c"]
        sources = [str(ROOT / "toolchain" / (name + ".cc")) for name in SOURCES]
        extra = ["-lntdll"] if os.name == "nt" else []
        for output, additions in ((library, ["-shared", *(["-Wl,/export:cupidbuild_verify_artifact_sizes", "-Wl,/export:cupidbuild_verify_artifact_sizes_selected"] if os.name == "nt" else ["-fPIC"])]),
                                  (cls.cli, [str(ROOT / "toolchain/cupidbuild_main.cc")])):
            result = subprocess.run([*common, *sources, *additions, *extra, "-o", str(output)],
                                    capture_output=True, timeout=300)
            if result.returncode:
                raise AssertionError(result.stderr.decode(errors="replace"))
        cls.library = ctypes.CDLL(str(library))
        if os.name == "nt":
            cls.addClassCleanup(cls.unload_library)
        cls.verify = cls.library.cupidbuild_verify_artifact_sizes
        cls.verify.argtypes = [ctypes.POINTER(Request), ctypes.POINTER(Result), ctypes.c_void_p, ctypes.c_size_t]
        cls.verify.restype = ctypes.c_int
        cls.verify_selected = cls.library.cupidbuild_verify_artifact_sizes_selected
        cls.verify_selected.argtypes = [ctypes.POINTER(Request), ctypes.c_char_p,
                                       ctypes.c_char_p, ctypes.POINTER(Result),
                                       ctypes.c_void_p, ctypes.c_size_t]
        cls.verify_selected.restype = ctypes.c_int

    @classmethod
    def unload_library(cls):
        import _ctypes
        _ctypes.FreeLibrary(cls.library._handle)
        cls.library._handle = 0

    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(prefix="artifact-fixture-")
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        shutil.copytree(ROOT / "bootstrap/seeds", self.root / "bootstrap/seeds")
        self.policy = Path("bootstrap/artifact-size-policy.json")
        self.manifest = Path("bootstrap/seeds/i386-linux/manifest.json")
        payload = (ROOT / self.policy).read_bytes()
        (self.root / self.policy).write_bytes(payload)
        for row in json.loads(payload)["artifacts"]:
            path = self.root / row["path"]
            if not path.exists():
                path.parent.mkdir(parents=True, exist_ok=True)
                with path.open("wb") as stream:
                    stream.truncate(row["exact_bytes"])

    def run_cli(self, *extra):
        return subprocess.run([str(self.cli), "verify-artifact-sizes", "--root", str(self.root),
                               "--policy", self.policy.as_posix(), "--seed-manifest", self.manifest.as_posix(), *extra],
                              capture_output=True, timeout=120)

    def request(self):
        return Request(str(self.root).encode(), self.policy.as_posix().encode(), self.manifest.as_posix().encode())

    def selected_cli(self, execution=None, checked=None):
        return self.run_cli(
            "--checked-manifest", checked or "bootstrap/seeds/i386-windows/manifest.json",
            "--execution-manifest", execution or
            ("bootstrap/seeds/i386-windows/manifest.json" if os.name == "nt"
             else self.manifest.as_posix()))

    def test_selected_execution_accepts_current_cohort(self):
        result = self.selected_cli()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, self.run_cli().stdout)

    def test_selected_execution_rejects_unchecked_windows_path(self):
        result = self.selected_cli(checked="alternate/manifest.json")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, b"")
        self.assertIn(b"not the production Windows manifest", result.stderr)

    def test_selected_execution_requires_both_options(self):
        for option in ("--checked-manifest", "--execution-manifest"):
            with self.subTest(option=option):
                result = self.run_cli(option, self.manifest.as_posix())
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, b"")

    def test_selected_execution_alternate_copy_and_payload_rejection(self):
        source = "i386-windows" if os.name == "nt" else "i386-linux"
        shutil.copytree(self.root / "bootstrap/seeds" / source, self.root / "alternate")
        result = self.selected_cli(execution="alternate/manifest.json")
        if os.name == "nt":
            self.assertEqual(result.returncode, 1)
            self.assertEqual(result.stdout, b"")
            self.assertIn(b"Windows execution seed is not the checked Windows seed", result.stderr)
            return
        self.assertEqual(result.returncode, 0, result.stderr)
        payload = self.root / "alternate/cupidc.elf"
        original = payload.read_bytes()
        stamp = payload.stat()
        payload.write_bytes(original[:-1] + bytes([original[-1] ^ 1]))
        os.utime(payload, ns=(stamp.st_atime_ns, stamp.st_mtime_ns))
        result = self.selected_cli(execution="alternate/manifest.json")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, b"")
        self.assertIn(b"execution seed image differs", result.stderr)
        payload.write_bytes(original)
        self.assertEqual(self.selected_cli(execution="alternate/manifest.json").returncode, 0)

    def assert_oracle_failure(self):
        with self.assertRaises(oracle.SizePolicyError) as raised:
            oracle.verify(self.root, self.policy, self.manifest)
        result = self.run_cli()
        self.assertEqual(result.returncode, 1)
        self.assertEqual(result.stdout, b"")
        self.assertEqual(result.stderr.replace(b"\r\n", b"\n"),
                         ("artifact size verification failed: " + str(raised.exception) + "\n").encode())

    def test_allocation_failures_release_state_and_allow_recovery(self):
        compiler = shutil.which("clang" if os.name == "nt" else "cc")
        program = Path(self.build.name) / ("allocations.exe" if os.name == "nt" else "allocations")
        sources = [str(ROOT / "toolchain" / (name + ".cc"))
                   for name in SOURCES if name != "cupidbuild_artifacts"]
        command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                   "-D_CRT_SECURE_NO_WARNINGS", "-I", str(ROOT / "toolchain"), "-x", "c",
                   str(ROOT / "toolchain/tests/cupidbuild_artifact_allocation_contract.cc"),
                   *sources, *(["-lntdll"] if os.name == "nt" else []), "-o", str(program)]
        built = subprocess.run(command, capture_output=True, timeout=300)
        self.assertEqual(built.returncode, 0, built.stderr.decode(errors="replace"))

        def inventory():
            import hashlib
            return {path.relative_to(self.root).as_posix():
                    hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None
                    for path in self.root.rglob("*")}

        before = inventory()
        result = subprocess.run([str(program), str(self.root)], capture_output=True, timeout=300)
        self.assertEqual((result.returncode, result.stderr), (0, b""), result.stderr)
        import re
        counts = re.fullmatch(rb"allocations=(\d+) failure_recovery_pairs=(\d+)\r?\n", result.stdout)
        self.assertIsNotNone(counts, result.stdout)
        self.assertGreaterEqual(int(counts[1]), 3)
        self.assertEqual(int(counts[2]), 5 * int(counts[1]))
        self.assertEqual(inventory(), before)

    def build_race_caller(self):
        compiler = shutil.which("clang" if os.name == "nt" else "cc")
        program = Path(self.build.name) / ("races.exe" if os.name == "nt" else "races")
        sources = [str(ROOT / "toolchain" / (name + ".cc"))
                   for name in SOURCES if name != "cupidbuild_artifacts"]
        built = subprocess.run(
            [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
             "-D_CRT_SECURE_NO_WARNINGS", "-I", str(ROOT / "toolchain"), "-x", "c",
             str(ROOT / "toolchain/tests/cupidbuild_artifact_race_contract.cc"),
             *sources, *(["-lntdll"] if os.name == "nt" else []), "-o", str(program)],
            capture_output=True, timeout=300)
        self.assertEqual(built.returncode, 0, built.stderr.decode(errors="replace"))
        return program

    def test_retained_inputs_reject_drift_before_success(self):
        import queue
        import threading

        program = self.build_race_caller()

        def command(root, phase, selected=False):
            arguments = [str(program), str(root), self.policy.as_posix(), self.manifest.as_posix(), phase]
            if selected:
                arguments.extend(["bootstrap/seeds/i386-windows/manifest.json", "alternate/manifest.json"])
            return arguments

        baseline = subprocess.run(command(self.root, "none"), capture_output=True, timeout=120)
        self.assertEqual((baseline.returncode, baseline.stderr), (0, b""))
        self.assertEqual(baseline.stdout.replace(b"\r\n", b"\n"),
                         b"Cupid artifact sizes: ok (16 exact artifacts)\n")
        cases = [("bootstrap/seeds/release.json", "payload"),
                 (self.policy.as_posix(), "payload"),
                 (self.manifest.as_posix(), "payload"),
                 ("bootstrap/seeds/i386-windows/manifest.json", "payload"),
                 ("bootstrap/seeds/i386-linux/cupidc.elf", "payload"),
                 ("bootstrap/seeds/i386-windows/cupidc.exe", "payload"),
                 ("kernel/kernel.bin", "size"),
                 ("bootstrap/seeds/i386-linux/extra", "extra")]
        if os.name != "nt":
            shutil.copytree(self.root / "bootstrap/seeds/i386-linux", self.root / "alternate")
            baseline = subprocess.run(command(self.root, "none", True), capture_output=True, timeout=120)
            self.assertEqual((baseline.returncode, baseline.stderr), (0, b""))
            cases.extend([("alternate/manifest.json", "payload"),
                          ("alternate/cupidc.elf", "payload"),
                          ("alternate/extra", "extra")])
        for phase in ("validate", "success"):
            for index, (logical, kind) in enumerate(cases):
                if phase == "validate" and index >= 4:
                    continue
                with self.subTest(phase=phase, path=logical), tempfile.TemporaryDirectory() as temporary:
                    root = Path(temporary) / "fixture"
                    shutil.copytree(self.root, root)
                    child = subprocess.Popen(command(root, phase, logical.startswith("alternate/")), stdin=subprocess.PIPE,
                                             stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    messages = queue.Queue()
                    reader = threading.Thread(target=lambda: messages.put(child.stderr.readline()), daemon=True)
                    reader.start()
                    try:
                        ready = messages.get(timeout=60)
                        self.assertEqual(ready.replace(b"\r\n", b"\n"), ("ready " + phase + "\n").encode())
                        target = root / logical
                        if kind == "payload":
                            before = target.stat()
                            payload = bytearray(target.read_bytes())
                            payload[-1] ^= 1
                            with target.open("r+b") as stream:
                                stream.write(payload)
                            os.utime(target, ns=(before.st_atime_ns, before.st_mtime_ns))
                            self.assertEqual((target.stat().st_size, target.stat().st_mtime_ns),
                                             (before.st_size, before.st_mtime_ns))
                        elif kind == "size":
                            with target.open("ab") as stream:
                                stream.write(b"x")
                        else:
                            target.write_bytes(b"extra")
                        stdout, stderr = child.communicate(b"x", timeout=120)
                        self.assertEqual(child.returncode, 1, stderr)
                        self.assertEqual(stdout, b"")
                        self.assertIn(b"artifact size verification failed:", stderr)
                    finally:
                        if child.poll() is None:
                            child.kill()
                            child.communicate()
                        reader.join(timeout=5)

    def test_success(self):
        result = self.run_cli()
        self.assertEqual((result.returncode, result.stderr), (0, b""))
        self.assertEqual(result.stdout.replace(b"\r\n", b"\n"), b"Cupid artifact sizes: ok (16 exact artifacts)\n")

    def test_all_size_failures_match_oracle(self):
        for name in ("kernel/kernel.bin", "kernel/kernel.elf"):
            path = self.root / name
            with path.open("r+b") as stream:
                stream.truncate(path.stat().st_size + 7)
        self.assert_oracle_failure()

    def test_missing_and_directory_diagnostics_match_oracle(self):
        (self.root / "kernel/kernel.bin").unlink()
        path = self.root / "kernel/kernel.elf"
        path.unlink()
        path.mkdir()
        self.assert_oracle_failure()

    def test_non_directory_parent_matches_oracle(self):
        path = self.root / "kernel"
        path.rename(self.root / "saved-kernel")
        path.write_bytes(b"not a directory")
        self.assert_oracle_failure()

    def test_release_mismatch_fails_without_success_output(self):
        path = self.root / "bootstrap/seeds/release.json"
        record = json.loads(path.read_bytes())
        record["source_revision"] = "0" * 40
        path.write_text(json.dumps(record), encoding="utf-8")
        result = self.run_cli()
        self.assertEqual((result.returncode, result.stdout), (1, b""))
        self.assertTrue(result.stderr.startswith(b"artifact size verification failed:"))

    def test_cli_rejects_unknown_duplicate_and_missing_values(self):
        for extra in (("--unknown",), ("--root", str(self.root)), ("--policy",)):
            with self.subTest(extra=extra):
                result = self.run_cli(*extra)
                self.assertEqual((result.returncode, result.stdout), (2, b""))
        result = subprocess.run([str(self.cli), "verify-artifact-sizes"], capture_output=True, timeout=30)
        self.assertEqual((result.returncode, result.stdout), (2, b""))

    def test_failed_result_clear_bounded_errors_and_recovery(self):
        path = self.root / "kernel/kernel.bin"
        size = path.stat().st_size
        path.unlink()
        full = ctypes.create_string_buffer(4096)
        result = Result(99, 99)
        request = self.request()
        self.assertEqual(self.verify(ctypes.byref(request), ctypes.byref(result), full, len(full)), 0)
        self.assertEqual((result.count, result.bytes), (0, 0))
        expected = full.value
        for capacity in (0, 1, 2, 9, 512):
            guard = ctypes.create_string_buffer(b"Z" * (capacity + 8))
            result = Result(99, 99)
            self.assertEqual(self.verify(ctypes.byref(request), ctypes.byref(result), guard if capacity else None, capacity), 0)
            self.assertEqual((result.count, result.bytes), (0, 0))
            if capacity:
                self.assertEqual(guard.raw[:capacity].split(b"\0")[0], expected[:capacity - 1])
                self.assertIn(b"\0", guard.raw[:capacity])
            self.assertEqual(guard.raw[capacity:capacity + 8], b"Z" * 8)
        with path.open("wb") as stream:
            stream.truncate(size)
        self.assertEqual(self.verify(ctypes.byref(request), ctypes.byref(result), None, 0), 1)
        self.assertEqual(result.count, 16)

    def test_invalid_arguments_clear_result(self):
        result = Result(99, 99)
        error = ctypes.create_string_buffer(80)
        self.assertEqual(self.verify(None, ctypes.byref(result), error, len(error)), 0)
        self.assertEqual((result.count, result.bytes), (0, 0))
        self.assertIn(b"invalid", error.value)
        request = self.request()
        self.assertEqual(self.verify(ctypes.byref(request), None, error, len(error)), 0)
        result = Result(99, 99)
        self.assertEqual(self.verify(ctypes.byref(request), ctypes.byref(result), None, 1), 0)
        self.assertEqual((result.count, result.bytes), (0, 0))

    def test_selected_api_invalid_paths_bound_diagnostics_and_recover(self):
        request = self.request()
        checked = b"bootstrap/seeds/i386-windows/manifest.json"
        execution = checked if os.name == "nt" else self.manifest.as_posix().encode()
        invalid = [(None, execution), (checked, None), (b"", execution),
                   (checked, b""), (b"alternate/manifest.json", execution),
                   (checked, b"x" * 8192)]
        if os.name != "nt":
            invalid.append((checked, b"bootstrap/seeds/i386-linux/other.json"))
        for selected_checked, selected_execution in invalid:
            full = ctypes.create_string_buffer(512)
            result = Result(99, 99)
            self.assertEqual(self.verify_selected(ctypes.byref(request), selected_checked,
                             selected_execution, ctypes.byref(result), full, len(full)), 0)
            expected = full.value
            self.assertTrue(expected)
            for capacity in (0, 1, 2, 9):
                with self.subTest(checked=selected_checked, execution=selected_execution, capacity=capacity):
                    result = Result(99, 99)
                    guard = ctypes.create_string_buffer(b"Z" * (capacity + 8))
                    self.assertEqual(self.verify_selected(ctypes.byref(request), selected_checked,
                                     selected_execution, ctypes.byref(result), guard if capacity else None, capacity), 0)
                    self.assertEqual((result.count, result.bytes), (0, 0))
                    if capacity:
                        self.assertEqual(guard.raw[:capacity].split(b"\0")[0], expected[:capacity-1])
                        self.assertIn(b"\0", guard.raw[:capacity])
                    self.assertEqual(guard.raw[capacity:capacity+8], b"Z" * 8)
        self.assertEqual(self.verify_selected(ctypes.byref(request), checked, execution,
                         ctypes.byref(result), None, 0), 1)
        self.assertEqual(result.count, 16)

if __name__ == "__main__":
    unittest.main()

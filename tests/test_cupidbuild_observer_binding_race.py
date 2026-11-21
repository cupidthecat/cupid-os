## @file
#  @brief Check source bindings when observed paths change.

"""Actual borrowed-observer drift at the transaction's rename boundaries."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
import unittest

from . import test_cupidbuild_observer as observer


class CupidBuildObserverBindingRaceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        configured = os.environ.get("CUPIDBUILD_OBSERVER_BINDING_RACE_PROGRAM")
        if configured:
            cls.program = Path(configured).resolve(strict=True)
            return
        cls.build = tempfile.TemporaryDirectory(prefix="cupid-observer-binding-race-")
        cls.addClassCleanup(cls.build.cleanup)
        directory = Path(cls.build.name)
        caller = directory / "caller.cc"
        caller.write_text(observer.CALLER, encoding="ascii")
        cls.program = directory / ("race.exe" if os.name == "nt" else "race.elf")
        compiler = shutil.which("clang")
        if not compiler:
            raise RuntimeError("Clang is required for the native publication race contract")
        command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                   "-D_CRT_SECURE_NO_WARNINGS", "-DCUPIDBUILD_PUBLICATION_RACE_TEST",
                   "-I", str(observer.ROOT / "toolchain"), "-x", "c", str(caller),
                   str(observer.ROOT / "toolchain/cupidbuild_host.cc"), str(observer.ROOT / "toolchain/path_encoding.cc"),
                   *(["-DNATIVE_OBSERVER_WINDOWS_WRITER", "-lntdll"] if os.name == "nt" else []),
                   "-o", str(cls.program)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=180)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="cupid-observer-binding-case-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.parent = self.root / "nested"
        self.parent.mkdir()

    prepare_binding = observer.CupidBuildObserverTests.prepare_binding
    binding = observer.CupidBuildObserverTests.binding
    prepare_source_requests = observer.CupidBuildObserverTests.prepare_source_requests
    make_source_junction = observer.CupidBuildObserverTests.make_source_junction
    remove_source_junction = observer.CupidBuildObserverTests.remove_source_junction

    def race(self, phase):
        base = self.root
        for existing in (True, False):
            with self.subTest(existing=existing):
                self.root = base / ("existing" if existing else "absent")
                self.parent = self.root / "nested"
                self.parent.mkdir(parents=True)
                self.prepare_binding()
                output = self.parent / "file.o"
                if not existing:
                    output.unlink()
                before = (output.read_bytes(), output.stat().st_mtime_ns) if existing else None
                ready, resume = self.root / "ready", self.root / "resume"
                environment = dict(os.environ)
                environment.update(CUPIDBUILD_PUBLICATION_TEST_PHASE=phase,
                                   CUPIDBUILD_PUBLICATION_TEST_READY=str(ready),
                                   CUPIDBUILD_PUBLICATION_TEST_RESUME=str(resume))
                process = subprocess.Popen([str(self.program), str(self.root).encode().hex(),
                                            "binding-publish", ""], stdin=subprocess.PIPE,
                                           stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                           text=True, encoding="utf-8", env=environment)
                try:
                    process.stdin.write("x")
                    process.stdin.flush()
                    deadline = time.monotonic() + 20
                    while not ready.exists() and process.poll() is None and time.monotonic() < deadline:
                        time.sleep(0.01)
                    self.assertTrue(ready.exists(), "publication checkpoint was not reached")
                    if phase == "after-install" and os.name != "nt":
                        self.assertEqual(output.read_bytes(), b"new")
                    added = self.root / "observed/empty/extra"
                    added.write_bytes(b"drift after validation")
                    resume.write_bytes(b"continue")
                    stdout, stderr = process.communicate(timeout=20)
                    self.assertEqual(process.returncode, 3, (stdout, stderr))
                    self.assertIn("directory membership changed", stderr)
                    if existing:
                        self.assertEqual((output.read_bytes(), output.stat().st_mtime_ns), before)
                    else:
                        self.assertFalse(output.exists())
                    self.assertFalse(list(self.root.rglob(".cupidbuild*")))
                    added.unlink()
                    ready.unlink()
                    resume.unlink()
                    if not existing:
                        output.write_bytes(b"old")
                    self.binding()
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.communicate(timeout=10)
                    for stream in (process.stdin, process.stdout, process.stderr):
                        stream.close()

    def test_observer_drift_before_mutation_rolls_back_and_recovers(self):
        self.race("before-mutation")

    def test_observer_drift_after_install_rolls_back_and_recovers(self):
        self.race("after-install")

    def test_original_source_request_changes_roll_back_and_recover(self):
        base = self.root
        for phase in ('before-mutation', 'after-install'):
            for mutation in ('missing', 'alias'):
                for existing in (False, True):
                    with self.subTest(phase=phase, mutation=mutation, existing=existing):
                        self.root = base / f'{phase}-{mutation}-{existing}'
                        self.parent = self.root / 'nested'
                        self.parent.mkdir(parents=True)
                        self.prepare_source_requests()
                        self.source_request_race(phase, mutation, existing)

    def source_request_race(self, phase, mutation, existing):
        output = self.parent / 'file.o'
        if existing:
            output.write_bytes(b'old')
            before = output.stat()
        missing = self.root / 'data/optional.bin'
        ready, resume = self.root / 'ready', self.root / 'resume'
        environment = dict(os.environ)
        environment.update(CUPIDBUILD_PUBLICATION_TEST_PHASE=phase,
                           CUPIDBUILD_PUBLICATION_TEST_READY=str(ready),
                           CUPIDBUILD_PUBLICATION_TEST_RESUME=str(resume))
        command = [str(self.program), str(self.root).encode('utf-8').hex(),
                   'source-requests-publish', '']
        with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                              text=True, encoding='utf-8', env=environment) as process:
            try:
                deadline = time.monotonic() + 20
                while not ready.exists() and process.poll() is None and time.monotonic() < deadline:
                    time.sleep(0.01)
                if process.poll() is not None:
                    self.fail(f'publisher exited before checkpoint: {process.communicate()}')
                self.assertTrue(ready.exists(), 'publication checkpoint was not reached')
                if mutation == 'missing':
                    missing.write_bytes(b'late')
                    diagnostic = 'missing source appeared'
                else:
                    self.remove_source_junction()
                    self.make_source_junction(self.root / 'substitute')
                    diagnostic = 'original source path changed'
                resume.touch()
                stdout, stderr = process.communicate(timeout=20)
                self.assertEqual(process.returncode, 3, (stdout, stderr))
                self.assertIn(diagnostic, stderr)
            finally:
                if process.poll() is None:
                    process.kill()
                    process.communicate(timeout=10)
        if existing:
            self.assertEqual(output.read_bytes(), b'old')
            after = output.stat()
            self.assertEqual((after.st_ino, after.st_mtime_ns), (before.st_ino, before.st_mtime_ns))
        else:
            self.assertFalse(output.exists())
        self.assertFalse(list(self.parent.glob('*cupidbuild*')))
        if mutation == 'missing':
            missing.unlink()
        else:
            self.remove_source_junction()
            self.make_source_junction(self.root / 'data')
        result = subprocess.run(command, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, (result.stdout, result.stderr))
        self.assertEqual(output.read_bytes(), b'new')
        self.assertFalse(list(self.parent.glob('*cupidbuild*')))

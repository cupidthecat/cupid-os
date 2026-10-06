"""Streamed payload observations at the existing guarded transaction boundary."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

from . import test_cupidbuild_observer as observer

# Keep the transaction/writer fixture shared with the ordinary observer suite.
# Only its payload observation uses the new streaming operation.
OLD_CAPTURE = '''cupidbuild_host_observer_file(observer, wrong_root ? "payload.dat" : "observed/payload.dat",
                                             64u, &bytes, &size) && size == 4u'''
NEW_CAPTURE = '''cupidbuild_host_observer_file_stream(observer,
                   wrong_root ? "payload.dat" : "observed/payload.dat",
                   (uint64_t)-1, NULL, NULL, &stream) && stream.size == 4u'''
assert observer.CALLER.count(OLD_CAPTURE) == 1
CALLER = observer.CALLER.replace(OLD_CAPTURE, NEW_CAPTURE).replace(
    'static int binding_case(const char *root, const char *mode) {',
    'static int binding_case(const char *root, const char *mode) {\n'
    '  cupidbuild_host_stream_observation_t stream;')


class CupidBuildObserverStreamBindingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if configured := os.environ.get('CUPIDBUILD_STREAM_BINDING_PROGRAM'):
            cls.program = Path(configured).resolve(strict=True)
            return
        cls.build = tempfile.TemporaryDirectory(prefix='cupid-stream-binding-')
        cls.addClassCleanup(cls.build.cleanup)
        directory = Path(cls.build.name)
        caller = directory / 'caller.cc'
        caller.write_text(CALLER, encoding='ascii')
        compiler = shutil.which('clang')
        if not compiler: raise RuntimeError('Clang is required for the native binding contract')
        cls.program = directory / ('stream-binding.exe' if os.name == 'nt' else 'stream-binding.elf')
        command = [compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                   '-D_CRT_SECURE_NO_WARNINGS', '-I', str(observer.ROOT / 'toolchain'), '-x', 'c',
                   str(caller), str(observer.ROOT / 'toolchain/cupidbuild_host.cc'),
                   str(observer.ROOT / 'toolchain/path_encoding.cc'),
                   *(['-DNATIVE_OBSERVER_WINDOWS_WRITER', '-lntdll'] if os.name == 'nt' else []),
                   '-o', str(cls.program)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=180)
        if result.returncode: raise AssertionError(result.stdout + result.stderr)

    setUp = observer.CupidBuildObserverTests.setUp
    prepare_binding = observer.CupidBuildObserverTests.prepare_binding
    binding = observer.CupidBuildObserverTests.binding

    test_publication_and_equal_timestamp_reuse = observer.CupidBuildObserverTests.test_bound_observer_publication_and_equal_timestamp_reuse
    test_unobserved_namespace_writes = observer.CupidBuildObserverTests.test_bound_observer_ignores_unobserved_namespace_writes
    test_other_root_rejection_and_recovery = observer.CupidBuildObserverTests.test_bound_observer_rejects_other_root_and_recovers
    test_null_poison_repeat_and_recovery = observer.CupidBuildObserverTests.test_bound_observer_rejects_null_poison_and_repeat
    test_binding_after_changed_publication = observer.CupidBuildObserverTests.test_bound_observer_rejects_binding_after_changed_publication
    test_binding_after_equal_output_reuse = observer.CupidBuildObserverTests.test_bound_observer_rejects_binding_after_equal_output_reuse
    test_restored_timestamp_payload_drift_and_recovery = observer.CupidBuildObserverTests.test_bound_observer_rejects_payload_drift_and_recovers
    test_added_directory_member_and_recovery = observer.CupidBuildObserverTests.test_bound_observer_rejects_added_directory_member_and_recovers
    test_empty_directory_drift_and_recovery = observer.CupidBuildObserverTests.test_bound_observer_rejects_empty_directory_drift_and_recovers
    test_explicit_publication_boundary = observer.CupidBuildObserverTests.test_bound_observer_explicit_boundary_rejects_membership_drift


if __name__ == '__main__': unittest.main()

from pathlib import Path
import contextlib
import io
import json
import os
import sys
import time
import traceback
import unittest
import tempfile
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(__file__).resolve().parent
sys.path.insert(0, str(BASE))
from consumer_audit_common import read, identity, git_bytes, SEED_FILES
sys.path.insert(0, str(ROOT))
from tests import test_cupidbuild_observer as observer
from tests import test_cupidbuild_observer_stream as stream

host, suffix = ('windows', 'exe') if os.name == 'nt' else ('linux', 'elf')
products = BASE / ('products-' + host + '3')
build_bytes = read(products / 'closed.json')
build = json.loads(build_bytes)
assert build['status'] == 'pass'
generation = sys.argv[1] if len(sys.argv) == 2 else '1'
assert generation.isdigit() and len(sys.argv) <= 2
output = BASE / ('legacy-' + host + generation)
assert not output.exists()
output.mkdir()
case_root = output / 'cases' if os.name == 'nt' else Path(tempfile.mkdtemp(prefix='cupid-retained-legacy-', dir='/var/tmp'))
if os.name == 'nt':
    assert not case_root.exists()
    case_root.mkdir()
qualification = json.loads(read(BASE / 'source-controls.json'))
records = []
started = time.monotonic()
receipt = {'schema': 'cupid.native-retained-observer-legacy.v1', 'status': 'running', 'host': host,
    'original_build': identity(build_bytes), 'runner': identity(read(Path(__file__))), 'records': records,
    'observer_test_source': identity(read(Path(observer.__file__))), 'stream_test_source': identity(read(Path(stream.__file__))),
    'test_bodies_changed': False, 'original_test_timeouts_changed': False,
    'original_process_resource_limits_preserved': True, 'case_directory': str(case_root)}
if os.name != 'nt':
    import resource
    receipt['linux_address_space_limits'] = list(resource.getrlimit(resource.RLIMIT_AS))
    receipt['linux_descriptor_limits'] = list(resource.getrlimit(resource.RLIMIT_NOFILE))

def guard(committed=False):
    for role in ('native', 'checked'):
        for name in ('legacy-observer', 'legacy-stream'):
            assert identity(read(products / (role + '-' + name + '.' + suffix))) == build[role + '_images'][name]
    for name, facts in build['sources'].items():
        assert identity(read(products / 'source' / name)) == facts
    for name, facts in qualification['source_inputs'].items():
        data = read(ROOT / name)
        assert identity(data) == facts
        if committed:
            assert data == git_bytes(qualification['source_revision'], name)
    for name in SEED_FILES:
        assert read(ROOT / name) == git_bytes('HEAD', name)

class Result(unittest.TextTestResult):
    def addSuccess(self, test):
        records.append({'role': current_role, 'group': current_group, 'test': test.id(), 'status': 'pass'})
        super().addSuccess(test)
    def addSkip(self, test, reason):
        records.append({'role': current_role, 'group': current_group, 'test': test.id(), 'status': 'skip', 'reason': reason})
        super().addSkip(test, reason)
    def addFailure(self, test, error):
        records.append({'role': current_role, 'group': current_group, 'test': test.id(), 'status': 'fail',
            'traceback': self._exc_info_to_string(error, test)})
        super().addFailure(test, error)
    def addError(self, test, error):
        records.append({'role': current_role, 'group': current_group, 'test': test.id(), 'status': 'error',
            'traceback': self._exc_info_to_string(error, test)})
        super().addError(test, error)

try:
    guard(True)
    for current_role in ('native', 'checked'):
        for current_group, module, cls in (
            ('observer', observer, observer.CupidBuildObserverTests),
            ('stream', stream, stream.CupidBuildObserverStreamTests)):
            directory = case_root / (current_role + '-' + current_group)
            directory.mkdir()
            program = products / (current_role + '-legacy-' + current_group + '.' + suffix)
            environment = dict(os.environ)
            for key in ('CUPIDBUILD_OBSERVER_PROGRAM', 'CUPIDBUILD_OBSERVER_CHECKED',
                        'CUPIDBUILD_STREAM_PROGRAM', 'CUPIDBUILD_STREAM_PRODUCTS'):
                environment.pop(key, None)
            environment.update({name: 'forbidden-host-' + name for name in ('CC', 'CXX', 'CPP', 'AS', 'AR', 'LD', 'NM', 'OBJCOPY', 'NASM')})
            environment.update(TEMP=str(directory), TMP=str(directory), TMPDIR=str(directory))
            if current_group == 'stream':
                environment.update(CUPIDBUILD_STREAM_PROGRAM=str(program), CUPIDBUILD_STREAM_PRODUCTS=str(directory))
            elif current_role == 'checked':
                environment['CUPIDBUILD_OBSERVER_PROGRAM'] = str(program)
            def setup_native(test_class):
                test_class.program = program
            # The native observer keeps its original native-only skip for the
            # invalid stdio destination. Checked execution keeps that test.
            native_setup = patch.object(cls, 'setUpClass', classmethod(setup_native)) if (
                current_group == 'observer' and current_role == 'native') else contextlib.nullcontext()
            log = io.StringIO()
            before = time.monotonic()
            with patch.dict(os.environ, environment, clear=True), native_setup:
                suite = unittest.defaultTestLoader.loadTestsFromTestCase(cls)
                result = unittest.TextTestRunner(stream=log, verbosity=2, resultclass=Result).run(suite)
            text = log.getvalue()
            (output / (current_role + '-' + current_group + '.log')).write_text(text, encoding='utf-8', newline='\n')
            receipt.setdefault('selections', []).append({'role': current_role, 'group': current_group,
                'program': identity(read(program)), 'tests_run': result.testsRun, 'skips': len(result.skipped),
                'failures': len(result.failures), 'errors': len(result.errors),
                'seconds': time.monotonic() - before, 'log': identity(text.encode('utf-8'))})
            (output / 'running.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
            print(json.dumps(receipt['selections'][-1]), flush=True)
            assert result.wasSuccessful()
            guard()
    assert sum(row['tests_run'] for row in receipt['selections']) == 176
    guard(True)
    receipt.update(status='pass', selected_methods=176, passed_methods=sum(row['status'] == 'pass' for row in records),
        declared_skips=sum(row['status'] == 'skip' for row in records),
        normal_99_raw_committed_inputs_unchanged=True, installed_root_seed_files_unchanged=15)
except BaseException as error:
    receipt.update(status='fail', error=repr(error))
    traceback.print_exc()
receipt['seconds'] = time.monotonic() - started
(output / 'closed.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({key: receipt.get(key) for key in ('status', 'host', 'selected_methods', 'passed_methods', 'declared_skips', 'seconds', 'error')}))
raise SystemExit(0 if receipt['status'] == 'pass' else 1)

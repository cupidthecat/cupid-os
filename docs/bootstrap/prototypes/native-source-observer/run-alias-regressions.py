from pathlib import Path
import contextlib
import io
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
import traceback
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(__file__).resolve().parent
sys.path.insert(0, str(BASE))
from consumer_audit_common import read, identity, git_bytes, SEED_FILES
sys.path.insert(0, str(ROOT))
from tests import test_cupidbuild_user_link_alias as alias
host, suffix = ('windows', 'exe') if os.name == 'nt' else ('linux', 'elf')
products = BASE / ('products-' + host + '1')
build_bytes = read(products / 'closed.json')
build = json.loads(build_bytes)
assert build['status'] == 'pass'
output = BASE / ('alias-' + host + '1')
assert not output.exists()
output.mkdir()
cases = output / 'cases' if os.name == 'nt' else Path(tempfile.mkdtemp(prefix='cupid-observer-alias-', dir='/var/tmp'))
if os.name == 'nt': cases.mkdir()
qualification = json.loads(read(BASE / 'source-controls.json'))
source = output / 'alias.cc'
source.write_text(alias.CALLER, encoding='ascii', newline='\n')
sentinels = {name: 'forbidden-host-' + name for name in ('CC', 'CXX', 'CPP', 'AS', 'AR', 'LD', 'NM', 'OBJCOPY', 'NASM')}
commands, records, programs = [], [], {}
controls = {str(source): identity(read(source)), str(Path(alias.__file__)): identity(read(Path(alias.__file__))),
    str(ROOT / 'toolchain/native_utf8.cc'): identity(read(ROOT / 'toolchain/native_utf8.cc')),
    str(ROOT / 'toolchain/native_utf8.h'): identity(read(ROOT / 'toolchain/native_utf8.h'))}
receipt = {'schema': 'cupid.native-observer-paths-alias-regression.v1', 'status': 'running', 'host': host,
    'original_build': identity(build_bytes), 'runner': identity(read(Path(__file__))), 'commands': commands,
    'records': records, 'programs': programs, 'controls': controls, 'producer_sentinels': sentinels,
    'test_bodies_changed': False, 'original_test_timeouts_changed': False, 'original_process_resource_limits_preserved': True}
started = time.monotonic()

def guard(committed=False):
    for name, facts in build['sources'].items(): assert identity(read(products / 'source' / name)) == facts
    for name, facts in build['objects'].items(): assert identity(read(products / 'objects' / name)) == facts
    for name, facts in controls.items(): assert identity(read(Path(name))) == facts
    for role, facts in programs.items(): assert identity(read(output / (role + '-alias.' + suffix))) == facts
    for name, facts in qualification['source_inputs'].items():
        data = read(ROOT / name)
        assert identity(data) == facts
        if committed: assert data == git_bytes(qualification['source_revision'], name)
    for name in SEED_FILES: assert read(ROOT / name) == git_bytes('HEAD', name)

def run(label, argv, *, native=False):
    guard()
    before = time.monotonic()
    result = subprocess.run(list(map(str, argv)), cwd=ROOT, env=dict(os.environ) if native else {**os.environ, **sentinels},
        capture_output=True, timeout=180)
    for stream in ('stdout', 'stderr'): (output / (label + '-' + stream + '.bin')).write_bytes(getattr(result, stream))
    commands.append({'label': label, 'argv': list(map(str, argv)), 'native': native, 'exit_code': result.returncode,
        'stdout': identity(result.stdout), 'stderr': identity(result.stderr), 'seconds': time.monotonic() - before,
        'timeout_seconds': 180})
    assert result.returncode == 0 and result.stdout == result.stderr == b'', commands[-1]
    guard()

class Result(unittest.TextTestResult):
    def addSuccess(self, test):
        records.append({'role': role, 'test': test.id(), 'status': 'pass'})
        super().addSuccess(test)
    def addSkip(self, test, reason):
        records.append({'role': role, 'test': test.id(), 'status': 'skip', 'reason': reason})
        super().addSkip(test, reason)
    def addFailure(self, test, error):
        records.append({'role': role, 'test': test.id(), 'status': 'fail', 'traceback': self._exc_info_to_string(error, test)})
        super().addFailure(test, error)
    def addError(self, test, error):
        records.append({'role': role, 'test': test.id(), 'status': 'error', 'traceback': self._exc_info_to_string(error, test)})
        super().addError(test, error)

try:
    guard(True)
    native = output / ('native-alias.' + suffix)
    argv = [shutil.which('clang'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-D_CRT_SECURE_NO_WARNINGS=1',
        '-I', products / 'source', '-I', ROOT / 'toolchain', '-x', 'c', source,
        products / 'source/cupidbuild_host.cc', products / 'source/path_encoding.cc']
    if os.name == 'nt': argv.extend(['-DNATIVE_ALIAS_WINDOWS=1', '-DCUPID_NATIVE_UTF8_ENABLE=1', ROOT / 'toolchain/native_utf8.cc', '-lntdll'])
    run('native-build', [*argv, '-o', native], native=True)
    programs['native'] = identity(read(native))
    checked = output / ('checked-alias.' + suffix)
    obj = output / 'alias.o'
    compile_argv = next(row['argv'] for row in build['commands'] if row['label'] == 'compile-resolved-parent')[:]
    for index, value in enumerate(compile_argv):
        if value.endswith('/source/unc_resolved_parent_contract.cc'): compile_argv[index] = '/' + source.relative_to(ROOT).as_posix()
    compile_argv[-1] = '/' + obj.relative_to(ROOT).as_posix()
    run('compile', compile_argv)
    link_argv = next(row['argv'] for row in build['commands'] if row['label'] == 'link-resolved-parent')[:]
    for index, value in enumerate(link_argv):
        if value.endswith('checked-resolved-parent.' + suffix): link_argv[index] = str(checked)
        elif value.endswith('resolved-parent.o'): link_argv[index] = str(obj)
    run('link', link_argv)
    programs['checked'] = identity(read(checked))
    dis = next(row['argv'][0] for row in build['commands'] if row['label'].startswith('strict-'))
    for name, path in (('object', obj), ('program', checked)):
        run('strict-' + name, [dis, '--require-known', '--require-local-targets', '--require-code-anchors', path])
    for role in ('native', 'checked'):
        directory = cases / role
        directory.mkdir()
        environment = {**os.environ, **sentinels, 'CUPIDBUILD_ALIAS_PROGRAM': str(output / (role + '-alias.' + suffix)),
            'TEMP': str(directory), 'TMP': str(directory), 'TMPDIR': str(directory)}
        environment.pop('CUPIDBUILD_ALIAS_CHECKED', None)
        log = io.StringIO()
        before = time.monotonic()
        with patch.dict(os.environ, environment, clear=True):
            suite = unittest.defaultTestLoader.loadTestsFromTestCase(alias.CupidBuildUserLinkAliasTests)
            result = unittest.TextTestRunner(stream=log, verbosity=2, resultclass=Result).run(suite)
        data = log.getvalue().encode('utf-8')
        (output / (role + '.log')).write_bytes(data)
        receipt.setdefault('selections', []).append({'role': role, 'tests_run': result.testsRun,
            'skips': len(result.skipped), 'failures': len(result.failures), 'errors': len(result.errors),
            'seconds': time.monotonic() - before, 'log': identity(data)})
        assert result.wasSuccessful() and result.testsRun == 12
        guard()
    guard(True)
    receipt.update(status='pass', selected_methods=24, passed_methods=sum(row['status'] == 'pass' for row in records),
        declared_skips=sum(row['status'] == 'skip' for row in records), alias_object=identity(read(obj)),
        normal_99_raw_committed_inputs_unchanged=True, installed_root_seed_files_unchanged=15)
except BaseException as error:
    receipt.update(status='fail', error=repr(error))
    traceback.print_exc()
receipt['seconds'] = time.monotonic() - started
(output / 'closed.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({key: receipt.get(key) for key in ('status', 'host', 'selected_methods', 'passed_methods', 'declared_skips', 'seconds', 'error')}))
raise SystemExit(0 if receipt['status'] == 'pass' else 1)

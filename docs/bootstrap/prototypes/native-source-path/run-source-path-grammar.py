from pathlib import Path
import json
import os
import subprocess
import sys
import time
import traceback

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(__file__).resolve().parent
sys.path.insert(0, str(BASE))
from consumer_audit_common import read, identity, git_bytes, SEED_FILES
sys.path.insert(0, str(ROOT))
from tools import hostbuild
generation, output_generation = sys.argv[1:3]
host, suffix = ('windows', 'exe') if os.name == 'nt' else ('linux', 'elf')
products = BASE / ('products-' + host + generation)
OUTPUT = BASE / ('grammar-' + host + output_generation)
assert not OUTPUT.exists()
OUTPUT.mkdir()
build = json.loads(read(products / 'closed.json'))
assert build['status'] == 'pass'
oracle_path = BASE / 'oracles/closed.json'
oracle_bytes = read(oracle_path)
oracle = json.loads(oracle_bytes)
working_root = Path(oracle['working_root'])
proof = json.loads(read(ROOT / 'docs/bootstrap/evidence/native-observer-paths-20261010.json'))
programs = {role: products / (role + '-source-path.' + suffix) for role in ('native', 'checked')}
sentinels = {name: 'forbidden-host-' + name for name in ('CC', 'CXX', 'CPP', 'AS', 'AR', 'LD', 'NM', 'OBJCOPY', 'NASM')}
environment = {**os.environ, **sentinels}
records = []
receipt = {'schema': 'cupid.native-source-path-grammar.v1', 'status': 'running', 'host': host,
    'build': identity(read(products / 'closed.json')), 'oracle': identity(oracle_bytes), 'records': records,
    'runner': identity(read(Path(__file__))), 'producer_sentinels': sentinels,
    'timeout_seconds': 60, 'address_space_bytes': None if os.name == 'nt' else 32 * 1024 * 1024,
    'descriptor_limit': None if os.name == 'nt' else 10240}


def limits():
    import resource
    resource.setrlimit(resource.RLIMIT_AS, (32 * 1024 * 1024, 32 * 1024 * 1024))
    resource.setrlimit(resource.RLIMIT_NOFILE, (10240, 10240))


def guard():
    for role, path in programs.items(): assert identity(read(path)) == build['programs'][role]
    for name, facts in build['sources'].items(): assert identity(read(products / 'source' / name)) == facts
    assert read(oracle_path) == oracle_bytes
    for name, facts in oracle['source_inputs'].items(): assert identity(read(ROOT / name)) == facts
    for name, facts in proof['source_inputs'].items():
        data = read(ROOT / name)
        assert identity(data) == facts and data == git_bytes(proof['source_revision'], name)
    for name in SEED_FILES: assert read(ROOT / name) == git_bytes('HEAD', name)


started = time.monotonic()
try:
    guard()
    requests = ['', '///', '//', '/', 'ordinary//nested/../payload.bin',
        '/a//b//../c/', '///a/../../b/', './', '../', './a/..',
        'ordinary/payload.bin///', '././', 'ordinary/.////', '/./../',
        ' /./a/../b', 'a/.../../c', 'a/. /../c']
    if os.name == 'nt':
        requests.extend(('D:ordinary/payload.bin/', 'D:.', 'D:../x/', 'D:',
            'Z:ordinary/payload.bin/', '//server', '//server/', '//server//share/',
            '//server/share//a/../b/', '//?/C:/ordinary/payload.bin',
            '//./C:/ordinary/payload.bin', '//?/UNC/server/share',
            '//?/UNC/server/share/', '//?/UNC/server/share/a/../b/',
            '//?/C:/ordinary/../payload.bin/', 'C:ordinary/payload.bin/',
            'c:ordinary/payload.bin/', 'C:./', 'C:/a/../', 'C:////a//b//',
            'C:/' + 'x' * 8188, 'C:/' + 'x' * 8189))
        requests.extend(('/' * count for count in range(4, 9)))
        requests.extend(('//server//', '//server///share/', '//?/unc/server/share/',
            '//?/UNC/server', '//?/UNC/server/', '//./C:/', '//?/C:/',
            '//./C:', '//?/C:', '//?/UNC/', '//./device', '//?/device',
            '\\\\?\\C:\\a\\.\\b\\..\\c\\', '\\\\?\\UNC\\server\\share\\a\\.\\b\\..\\c\\',
            'D:ordinary//nested/../payload.bin///', 'Z:./', 'C:/ /a/../b/',
            '//server/share/a/. /../b/', '//server/share/a/.../../b/'))
    else:
        requests.extend(('\\leading-name', 'colon:name', 'backslash\\name',
            'ordinary/line\nname.bin', '//server//share/', '/a\\b/../c:d',
            '/' + 'x' * 8190, '/' + 'x' * 8191))
    previous = Path.cwd()
    try:
        os.chdir(working_root)
        cases = [{'request': text, 'absolute': str(hostbuild._disk_absolute(Path(text)))} for text in requests]
    finally:
        os.chdir(previous)
    for role in ('native', 'checked'):
        for index, case in enumerate(cases):
            request, absolute = case['request'], case['absolute']
            code = 0 if len(request.encode('utf-8')) < 8192 and len(absolute.encode('utf-8')) < 8192 else 2
            expected = absolute.encode('utf-8') if code == 0 else b''
            before = time.monotonic()
            result = subprocess.run([str(programs[role]), 'normal', str(working_root), request],
                cwd=working_root, env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                timeout=60, preexec_fn=None if os.name == 'nt' else limits)
            row = {'role': role, 'case': index, 'request': request, 'python_absolute': absolute,
                'expected_exit_code': code, 'expected_stdout': expected.decode('utf-8'),
                'exit_code': result.returncode, 'stdout': result.stdout.decode('utf-8'),
                'stderr': result.stderr.decode('utf-8'), 'seconds': time.monotonic() - before,
                'status': 'pass' if result.returncode == code and result.stdout == expected and not result.stderr else 'fail'}
            records.append(row)
            (OUTPUT / 'running.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    guard()
    failures = [row for row in records if row['status'] != 'pass']
    receipt.update(status='fail' if failures else 'pass', calls=len(records), cases_per_caller=len(cases),
        failures=len(failures), normal_99_raw_committed_inputs_unchanged=True, installed_root_seed_files_unchanged=15)
except BaseException as error:
    receipt.update(status='fail', error=repr(error), calls=len(records))
    traceback.print_exc()
receipt['seconds'] = time.monotonic() - started
(OUTPUT / 'closed.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({key: receipt.get(key) for key in ('status', 'host', 'calls', 'cases_per_caller', 'failures', 'seconds', 'error')}))
for row in records:
    if row['status'] != 'pass':
        print(json.dumps({key: row[key] for key in ('role', 'case', 'request', 'exit_code', 'stdout', 'python_absolute')}))
raise SystemExit(0 if receipt['status'] == 'pass' else 1)

from pathlib import Path
import json
import os
import subprocess
import sys
import time
import traceback

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(__file__).resolve().parent
generation = sys.argv[1] if len(sys.argv) > 1 else '2'
output_generation = sys.argv[2] if len(sys.argv) > 2 else '1'
host, suffix = ('windows', 'exe') if os.name == 'nt' else ('linux', 'elf')
products = BASE / ('products-' + host + generation)
OUTPUT = BASE / ('contracts-' + host + output_generation)
assert not OUTPUT.exists()
OUTPUT.mkdir()
sys.path.insert(0, str(BASE))
from consumer_audit_common import read, identity, git_bytes, SEED_FILES
sys.path.insert(0, str(ROOT))
from tools import hostbuild
build = json.loads(read(products / 'closed.json'))
assert build['status'] == 'pass'
oracle_path = BASE / 'oracles/closed.json'
oracle_bytes = read(oracle_path)
oracle = json.loads(oracle_bytes)
assert oracle['status'] == 'pass'
working_root = Path(oracle['working_root'])
for name, facts in oracle['source_inputs'].items(): assert identity(read(ROOT / name)) == facts
proof = json.loads(read(ROOT / 'docs/bootstrap/evidence/native-observer-paths-20261010.json'))
sentinels = {name: 'forbidden-host-' + name for name in ('CC', 'CXX', 'CPP', 'AS', 'AR', 'LD', 'NM', 'OBJCOPY', 'NASM')}
environment = {**os.environ, **sentinels}
programs = {role: products / (role + '-source-path.' + suffix) for role in ('native', 'checked')}
records = []
started = time.monotonic()
receipt = {'schema': 'cupid.native-source-path-contracts.v1', 'status': 'running', 'host': host,
    'build': identity(read(products / 'closed.json')), 'oracle': identity(oracle_bytes), 'records': records,
    'runner': identity(read(Path(__file__))), 'producer_sentinels': sentinels,
    'timeout_seconds': 60, 'address_space_bytes': None if os.name == 'nt' else 32 * 1024 * 1024,
    'descriptor_limit': None if os.name == 'nt' else 10240}


def limits():
    import resource
    resource.setrlimit(resource.RLIMIT_AS, (32 * 1024 * 1024, 32 * 1024 * 1024))
    resource.setrlimit(resource.RLIMIT_NOFILE, (10240, 10240))


def guard(committed=False):
    for role, path in programs.items(): assert identity(read(path)) == build['programs'][role]
    for name, facts in build['sources'].items(): assert identity(read(products / 'source' / name)) == facts
    assert read(oracle_path) == oracle_bytes
    for name, facts in oracle['source_inputs'].items(): assert identity(read(ROOT / name)) == facts
    for name, facts in proof['source_inputs'].items():
        data = read(ROOT / name)
        assert identity(data) == facts
        if committed: assert data == git_bytes(proof['source_revision'], name)
    for name in SEED_FILES: assert read(ROOT / name) == git_bytes('HEAD', name)


def invoke(role, label, mode, root, request, expected_code, expected_output):
    before = time.monotonic()
    result = subprocess.run([str(programs[role]), mode, root, request], cwd=working_root,
        env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60,
        preexec_fn=None if os.name == 'nt' else limits)
    row = {'role': role, 'label': label, 'mode': mode, 'root': root, 'request': request,
        'expected_exit_code': expected_code, 'expected_stdout': expected_output,
        'exit_code': result.returncode, 'stdout': result.stdout.decode('utf-8'),
        'stderr': result.stderr.decode('utf-8'), 'seconds': time.monotonic() - before,
        'timeout_seconds': 60, 'address_space_bytes': receipt['address_space_bytes'],
        'descriptor_limit': receipt['descriptor_limit']}
    row['status'] = 'pass' if result.returncode == expected_code and result.stdout == expected_output.encode('utf-8') and not result.stderr else 'fail'
    records.append(row)
    (OUTPUT / 'running.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    assert row['status'] == 'pass', row


try:
    guard(True)
    root = str(working_root)
    extra = ['', '.', '..', 'ordinary//nested/../payload.bin', '../../../ordinary/payload.bin',
        '/a//b/./c/../d/', '//a//b/./../c/', '///a//b/../c', 'ordinary/absent/../payload.bin']
    if os.name == 'nt':
        extra.extend(('C:ordinary/payload.bin', 'c:ordinary/payload.bin', 'C:', 'C:/', '\\ordinary\\payload.bin'))
    else:
        extra.extend(('\\leading-name', 'colon:name', 'backslash\\name', 'ordinary/line\nname.bin'))
    previous = Path.cwd()
    try:
        os.chdir(working_root)
        additional = [{'label': 'lexical-extra-' + str(index), 'spelling': text,
            'absolute': str(hostbuild._disk_absolute(Path(text)))} for index, text in enumerate(extra)]
    finally:
        os.chdir(previous)
    for role in ('native', 'checked'):
        for row in (*oracle['records'], *additional):
            invoke(role, row['label'], 'normal', root, row['spelling'], 0, row['absolute'])
        for mode in ('null-root', 'null-request', 'null-output', 'zero-capacity', 'short-capacity', 'bad-root', 'bad-request'):
            invoke(role, mode, mode, root, 'ordinary/payload.bin', 2, '')
        invoke(role, 'relative-root', 'normal', 'relative', 'payload.bin', 2, '')
        invoke(role, 'oversized-request', 'normal', root, 'x' * 8192, 2, '')
        invoke(role, 'unused-null-root', 'null-root', root, str(working_root / 'ordinary/payload.bin'), 0,
            str(hostbuild._disk_absolute(working_root / 'ordinary/payload.bin')))
    guard(True)
    receipt.update(status='pass', calls=len(records), actual_oracle_cases_per_caller=len(oracle['records']),
        additional_lexical_cases_per_caller=len(additional), negative_cases_per_caller=9,
        normal_99_raw_committed_inputs_unchanged=True, installed_root_seed_files_unchanged=15)
except BaseException as error:
    receipt.update(status='fail', error=repr(error), calls=len(records))
    traceback.print_exc()
receipt['seconds'] = time.monotonic() - started
(OUTPUT / 'closed.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({key: receipt.get(key) for key in ('status', 'host', 'calls', 'seconds', 'error')}))
raise SystemExit(0 if receipt['status'] == 'pass' else 1)

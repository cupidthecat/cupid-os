from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
import time
import traceback

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(__file__).resolve().parent
sys.path.insert(0, str(BASE))
from consumer_audit_common import read, identity, git_bytes, SEED_FILES
host, suffix = ('windows', 'exe') if os.name == 'nt' else ('linux', 'elf')
products = BASE / ('products-' + host + '1')
build_bytes = read(products / 'closed.json')
build = json.loads(build_bytes)
assert build['status'] == 'pass'
output = BASE / ('resolved-parent-' + host + '1')
assert not output.exists()
output.mkdir()
cases = output / 'cases' if os.name == 'nt' else Path(tempfile.mkdtemp(prefix='cupid-unc-resolved-parent-', dir='/var/tmp'))
if os.name == 'nt': cases.mkdir()
qualification = json.loads(read(BASE / 'source-controls.json'))
sentinels = {name: 'forbidden-host-' + name for name in ('CC', 'CXX', 'CPP', 'AS', 'AR', 'LD', 'NM', 'OBJCOPY', 'NASM')}
records = []
receipt = {'schema': 'cupid.native-unc-resolved-parent-contracts.v1', 'status': 'running', 'host': host,
    'original_build': identity(build_bytes), 'runner': identity(read(Path(__file__))), 'records': records,
    'producer_sentinels': sentinels, 'timeout_seconds': 60,
    'linux_address_space_bytes': 33554432, 'linux_descriptor_limit': 10240}
started = time.monotonic()

def guard(committed=False):
    for role in ('native', 'checked'):
        assert identity(read(products / (role + '-resolved-parent.' + suffix))) == build[role + '_images']['resolved-parent']
    for name, facts in build['sources'].items(): assert identity(read(products / 'source' / name)) == facts
    for name, facts in qualification['source_inputs'].items():
        data = read(ROOT / name)
        assert identity(data) == facts
        if committed: assert data == git_bytes(qualification['source_revision'], name)
    for name in SEED_FILES: assert read(ROOT / name) == git_bytes('HEAD', name)

def limits():
    import resource
    resource.setrlimit(resource.RLIMIT_AS, (33554432, 33554432))
    resource.setrlimit(resource.RLIMIT_NOFILE, (10240, 10240))

def unc(path, server='localhost'):
    return '\\\\' + server + '\\' + str(path)[0].upper() + '$\\' + str(path)[3:]

def run(role, label, *, spelling='ordinary', invalid=None):
    guard()
    directory = cases / (role + '-' + label)
    directory.mkdir()
    root = directory / 'root'
    root.mkdir()
    if spelling in ('unicode', 'long-unicode'):
        for index in range(1 if spelling == 'unicode' else 5):
            root = root / ('unicode-\u96ea-\U0001f431-' + str(index) + '-' + 'x' * 48)
            root.mkdir()
    (root / 'payload.bin').write_bytes(b'payload')
    selected = str(root)
    if os.name == 'nt':
        selected = unc(root, '127.0.0.1' if spelling == 'address' else 'localhost')
        if spelling == 'upper': selected = selected.upper()
        elif spelling == 'slashes': selected = selected.replace('\\', '/')
    if spelling == 'alias':
        alias = directory / 'alias'
        if os.name == 'nt':
            created = subprocess.run(['cmd.exe', '/c', 'mklink', '/J', str(alias), str(root)],
                capture_output=True, timeout=30)
            assert created.returncode == 0, created.stderr
            selected = unc(alias)
        else:
            alias.symlink_to(root, target_is_directory=True)
            selected = str(alias)
    if invalid == 'missing-root': selected = str(root / 'missing') if os.name != 'nt' else unc(root / 'missing')
    elif invalid == 'file-root': selected = str(root / 'payload.bin') if os.name != 'nt' else unc(root / 'payload.bin')
    elif invalid == 'invalid-utf8': selected = b'\xc0\xaf'
    elif invalid == 'missing-source': (root / 'payload.bin').unlink()
    elif invalid == 'linked-source':
        (root / 'payload.bin').unlink()
        (root / 'target.bin').write_bytes(b'payload')
        (root / 'payload.bin').symlink_to(root / 'target.bin')
    elif invalid == 'fifo-source':
        (root / 'payload.bin').unlink()
        os.mkfifo(root / 'payload.bin')
    encoded = selected if isinstance(selected, bytes) else selected.encode('utf-8')
    before = time.monotonic()
    program = products / (role + '-resolved-parent.' + suffix)
    argv = [str(program), encoded.hex(), 'reject' if invalid else 'resolve']
    result = subprocess.run(argv, cwd=ROOT, env={**os.environ, **sentinels}, capture_output=True,
        timeout=60, preexec_fn=limits if os.name != 'nt' else None)
    row = {'role': role, 'label': label, 'spelling': spelling, 'invalid': invalid, 'argv': argv,
        'exit_code': result.returncode, 'stdout': result.stdout.decode('utf-8', 'replace'),
        'stderr': result.stderr.decode('utf-8', 'replace'), 'stdout_identity': identity(result.stdout),
        'stderr_identity': identity(result.stderr), 'seconds': time.monotonic() - before,
        'namespace_created': (root / 'created.bin').exists(), 'timeout_seconds': 60}
    records.append(row)
    (output / 'running.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n')
    assert result.returncode == 0 and result.stdout.replace(b'\r\n', b'\n') == b'ok\n' and not result.stderr, row
    assert not row['namespace_created'], row
    guard()

try:
    guard(True)
    for role in ('native', 'checked'):
        for spelling in ('ordinary', 'unicode', 'long-unicode', 'alias'):
            run(role, 'resolve-' + spelling, spelling=spelling)
        if os.name == 'nt':
            for spelling in ('address', 'upper', 'slashes'): run(role, 'resolve-' + spelling, spelling=spelling)
        for invalid in ('missing-root', 'file-root', 'invalid-utf8', 'missing-source'):
            run(role, 'reject-' + invalid, invalid=invalid)
        if os.name != 'nt':
            for invalid in ('linked-source', 'fifo-source'): run(role, 'reject-' + invalid, invalid=invalid)
    native = [row for row in records if row['role'] == 'native']
    checked = [row for row in records if row['role'] == 'checked']
    assert len(native) == len(checked)
    for left, right in zip(native, checked):
        assert left['label'] == right['label']
        assert left['stdout'].replace('\r\n', '\n') == right['stdout'].replace('\r\n', '\n')
    guard(True)
    receipt.update(status='pass', calls=len(records), cases_per_caller=len(native),
        complete_native_checked_results_equal=True, normal_99_raw_committed_inputs_unchanged=True,
        installed_root_seed_files_unchanged=15)
except BaseException as error:
    receipt.update(status='fail', error=repr(error))
    traceback.print_exc()
receipt['seconds'] = time.monotonic() - started
(output / 'closed.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({key: receipt.get(key) for key in ('status', 'host', 'calls', 'cases_per_caller', 'seconds', 'error')}))
raise SystemExit(0 if receipt['status'] == 'pass' else 1)

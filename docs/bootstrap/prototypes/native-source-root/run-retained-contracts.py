from pathlib import Path
import hashlib
import json
import os
import queue
import subprocess
import sys
import tempfile
import threading
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
output = BASE / ('contracts-' + host + '1')
assert not output.exists()
output.mkdir()
cases_root = output / 'cases' if os.name == 'nt' else Path(tempfile.mkdtemp(prefix='cupid-missing-unc-retained-', dir='/var/tmp'))
if os.name == 'nt': cases_root.mkdir()
qualification = json.loads(read(BASE / 'source-controls.json'))
controls = {products / (role + '-retained.' + suffix): build[role + '_images']['retained']
            for role in ('native', 'checked')}
sentinels = {name: 'forbidden-host-' + name for name in ('CC', 'CXX', 'CPP', 'AS', 'AR', 'LD', 'NM', 'OBJCOPY', 'NASM')}
records = []
started = time.monotonic()
receipt = {'schema': 'cupid.native-retained-observer-contracts.v1', 'status': 'running', 'host': host,
    'original_build': identity(build_bytes), 'runner': identity(read(Path(__file__))),
    'producer_sentinels': sentinels, 'records': records, 'timeout_seconds': 60,
    'linux_address_space_bytes': 33554432, 'linux_descriptor_limit': 10240}

def guard(committed=False):
    for path, facts in controls.items():
        assert identity(read(path)) == facts
    for name, facts in build['sources'].items():
        assert identity(read(products / 'source' / name)) == facts
    for name, facts in qualification['source_inputs'].items():
        data = read(ROOT / name)
        assert identity(data) == facts
        if committed:
            assert data == git_bytes(qualification['source_revision'], name)
    for name in SEED_FILES:
        assert read(ROOT / name) == git_bytes('HEAD', name)

def limits():
    import resource
    resource.setrlimit(resource.RLIMIT_AS, (33554432, 33554432))
    resource.setrlimit(resource.RLIMIT_NOFILE, (10240, 10240))

def data(size):
    return bytes(index % 251 for index in range(size))

def run(role, label, mode, second='payload.bin', *, size=1, mutation=None, tree=None, unc_root=False):
    guard()
    directory = cases_root / (role + '-' + label)
    directory.mkdir()
    root = directory / 'root'
    root.mkdir()
    other = directory / 'other'
    other.mkdir()
    payload = root / 'payload.bin'
    contents = data(size)
    payload.write_bytes(contents)
    (root / 'different.bin').write_bytes(contents)
    os.link(payload, root / 'hardlink.bin')
    (root / 'nested').mkdir()
    (root / 'nested/payload.bin').write_bytes(contents)
    if tree:
        for index in range(4096):
            path = root / ('d%04d/payload.bin' % index if tree == 'nested' else 'f%04d.bin' % index)
            if tree == 'nested': path.parent.mkdir()
            path.write_bytes(b'\0')
    if second == ':root': second = str(root)
    if second == ':other': second = str(other)
    argument = second if isinstance(second, bytes) else second.encode('utf-8')
    program = products / (role + '-retained.' + suffix)
    selected_root = '\\\\localhost\\C$\\' + str(root)[3:] if unc_root else str(root)
    argv = [str(program), selected_root.encode('utf-8').hex(), mode, argument.hex()]
    before = time.monotonic()
    process = subprocess.Popen(argv, cwd=ROOT, env={**os.environ, **sentinels},
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        preexec_fn=limits if os.name != 'nt' else None)
    first = b''
    error = None
    custody_error = None
    try:
        if mutation:
            mailbox = queue.Queue()
            reader = threading.Thread(target=lambda: mailbox.put(process.stdout.readline()), daemon=True)
            reader.start()
            first = mailbox.get(timeout=max(0.1, 60 - (time.monotonic() - before)))
            assert first.replace(b'\r\n', b'\n') == b'ready\n', first
            if mutation == 'root':
                (root / 'appeared.bin').write_bytes(b'new')
            elif mutation == 'size':
                payload.write_bytes(b'longer')
            elif mutation == 'restored':
                stat = payload.stat()
                payload.write_bytes(b'!' + contents[1:])
                os.utime(payload, ns=(stat.st_atime_ns, stat.st_mtime_ns))
            elif mutation == 'rename-parent':
                (root / 'nested').rename(root / 'renamed')
                (root / 'nested').mkdir()
                (root / 'nested/payload.bin').write_bytes(contents)
            elif mutation == 'parent-custody':
                try:
                    (root / 'nested').rename(root / 'renamed')
                except PermissionError as caught:
                    assert caught.winerror in (5, 32), caught
                    custody_error = caught.winerror
                else: raise AssertionError('Windows directory custody did not reject replacement')
            else: raise ValueError(mutation)
            stdout, stderr = process.communicate(b'x', timeout=max(0.1, 60 - (time.monotonic() - before)))
        else:
            stdout, stderr = process.communicate(timeout=60)
    except BaseException as caught:
        error = repr(caught)
        if process.poll() is None: process.kill()
        stdout, stderr = process.communicate(timeout=10)
    stdout = first + stdout
    record = {'role': role, 'label': label, 'mode': mode, 'argv': argv, 'exit_code': process.returncode,
        'seconds': time.monotonic() - before, 'stdout': stdout.decode('utf-8', 'replace'),
        'stderr': stderr.decode('utf-8', 'replace'), 'stdout_identity': identity(stdout), 'stderr_identity': identity(stderr),
        'error': error, 'source_before': identity(contents), 'mutation': mutation,
        'tree_files': 4096 if tree else 0, 'timeout_seconds': 60,
        'address_space_bytes': 33554432 if os.name != 'nt' else None,
        'descriptor_limit': 10240 if os.name != 'nt' else None}
    if mutation == 'parent-custody' and not error:
        assert custody_error in (5, 32)
        (root / 'nested').rename(root / 'renamed')
        record.update(windows_replacement_rejected_while_retained=custody_error,
            windows_rename_succeeded_after_close=True)
    records.append(record)
    (output / 'running.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    assert not error and process.returncode == 0 and not stderr, record
    normalized = stdout.replace(b'\r\n', b'\n')
    if mode.startswith('capture-'):
        expected = ('capture 0 %d %s\nok\n' % (size, hashlib.sha256(contents).hexdigest())).encode()
        assert normalized == expected, record
    elif mode == 'default-limit':
        lines = normalized.decode().splitlines()
        assert len(lines) == 2 and lines[1] == 'ok' and lines[0].startswith('capacity ')
        assert 4000 < int(lines[0].split()[1]) < 4096
    elif mode == 'old-nested':
        lines = normalized.decode().splitlines()
        assert len(lines) == 2 and lines[1] == 'ok' and lines[0].startswith('old-stream-limit ')
        assert 0 < int(lines[0].split()[1]) < 4096
    else:
        assert normalized == (b'ready\nok\n' if mutation else b'ok\n'), record
    guard()

try:
    guard(True)
    for role in ('native', 'checked'):
        run(role, 'invalid-quotas', 'invalid-quotas')
        run(role, 'default-limit', 'default-limit', tree='flat')
        run(role, 'full-flat', 'quota-flat', tree='flat')
        run(role, 'full-nested', 'quota-nested', tree='nested')
        if os.name != 'nt': run(role, 'old-nested-control', 'old-nested', tree='nested')
        run(role, 'same-root', 'roots-equal', ':root')
        run(role, 'different-root', 'roots-different', ':other')
        run(role, 'null-root-result', 'roots-invalid', ':root')
        run(role, 'same-file', 'files-equal')
        run(role, 'hardlinked-file', 'files-equal', 'hardlink.bin')
        run(role, 'different-file', 'files-different', 'different.bin')
        run(role, 'unretained-file-comparison', 'files-unobserved')
        run(role, 'invalid-file-comparison', 'files-invalid')
        for size in (0, 1, 55, 56, 63, 64, 65, 65535, 65536, 65537, 1048577):
            run(role, 'capture-%d' % size, 'capture-file', size=size)
        for label, mode, path in (('unobserved-stream', 'reject-unobserved', 'payload.bin'),
                ('directory-stream', 'reject-directory', 'nested'), ('zero-limit', 'reject-limit', 'payload.bin'),
                ('sink-failure', 'reject-sink', 'payload.bin'), ('null-stream-result', 'reject-result', 'payload.bin')):
            run(role, label, mode, path)
        for index, path in enumerate(('', '.', '..', '../payload.bin', '/payload.bin',
                'nested//payload.bin', 'nested/payload.bin/', b'\xc0\xaf', b'\xed\xa0\x80')):
            run(role, 'invalid-path-%d' % index, 'reject-path', path)
        run(role, 'retained-size-drift', 'wait-stream', mutation='size')
        run(role, 'retained-digest-drift', 'wait-stream', mutation='restored')
        run(role, 'historical-root-versus-live', 'wait-root-identity', mutation='root')
        run(role, 'historical-file-versus-live', 'wait-file-identity', mutation='restored')
        run(role, 'retained-parent-replacement', 'wait-parent-custody' if os.name == 'nt' else 'wait-stream',
            'nested/payload.bin', mutation='parent-custody' if os.name == 'nt' else 'rename-parent')
        if os.name == 'nt':
            run(role, 'unc-capture-65537', 'capture-file', size=65537, unc_root=True)
            run(role, 'unc-retained-digest-drift', 'wait-stream', size=65535, mutation='restored', unc_root=True)
            run(role, 'unc-historical-root-versus-live', 'wait-root-identity', mutation='root', unc_root=True)
            run(role, 'unc-retained-parent-custody', 'wait-parent-custody', 'nested/payload.bin',
                mutation='parent-custody', unc_root=True)
    left = [record for record in records if record['role'] == 'native']
    right = [record for record in records if record['role'] == 'checked']
    assert len(left) == len(right)
    for native, checked in zip(left, right):
        assert native['label'] == checked['label'] and native['mode'] == checked['mode']
        assert native['stdout'].replace('\r\n', '\n') == checked['stdout'].replace('\r\n', '\n')
        assert native['stderr'] == checked['stderr'] == ''
    guard(True)
    receipt.update(status='pass', calls=len(records), cases_per_caller=len(left),
        complete_native_checked_results_equal=True, normal_99_raw_committed_inputs_unchanged=True,
        installed_root_seed_files_unchanged=15)
except BaseException as error:
    receipt.update(status='fail', error=repr(error))
    traceback.print_exc()
receipt['seconds'] = time.monotonic() - started
(output / 'closed.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({key: receipt.get(key) for key in ('status', 'host', 'calls', 'cases_per_caller', 'seconds', 'error')}))
raise SystemExit(0 if receipt['status'] == 'pass' else 1)

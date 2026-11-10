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
build_data = read(products / 'closed.json')
build = json.loads(build_data)
assert build['status'] == 'pass'
output = BASE / ('missing-unc-' + host + '1')
assert not output.exists()
output.mkdir()
cases = output / 'cases' if os.name == 'nt' else Path(tempfile.mkdtemp(prefix='cupid-missing-unc-', dir='/var/tmp'))
if os.name == 'nt': cases.mkdir()
qualification = json.loads(read(BASE / 'source-controls.json'))
sentinels = {name: 'forbidden-host-' + name for name in ('CC', 'CXX', 'CPP', 'AS', 'AR', 'LD', 'NM', 'OBJCOPY', 'NASM')}
records = []
receipt = {'schema': 'cupid.native-missing-unc-observer-contracts.v1', 'status': 'running', 'host': host,
    'original_build': identity(build_data), 'runner': identity(read(Path(__file__))), 'records': records,
    'producer_sentinels': sentinels, 'timeout_seconds': 60, 'case_directory': str(cases),
    'linux_address_space_bytes': 33554432, 'linux_descriptor_limit': 10240}
started = time.monotonic()

def guard(committed=False):
    for role in ('native', 'checked'):
        assert identity(read(products / (role + '-missing-unc.' + suffix))) == build[role + '_images']['missing-unc']
    for name, facts in build['sources'].items():
        assert identity(read(products / 'source' / name)) == facts
    for name, facts in qualification['source_inputs'].items():
        data = read(ROOT / name)
        assert identity(data) == facts
        if committed: assert data == git_bytes(qualification['source_revision'], name)
    for name in SEED_FILES:
        assert read(ROOT / name) == git_bytes('HEAD', name)

def limits():
    import resource
    resource.setrlimit(resource.RLIMIT_AS, (33554432, 33554432))
    resource.setrlimit(resource.RLIMIT_NOFILE, (10240, 10240))

def unc(path, server='localhost'):
    text = str(path)
    assert len(text) > 3 and text[1:3] == ':\\'
    return '\\\\' + server + '\\' + text[0].upper() + '$\\' + text[3:]

def run(role, label, mode, second='nested/missing.bin', *, size=1, tree=False,
        spelling='ordinary', mutation=None, unsafe_root=None, special=None):
    guard()
    directory = cases / (role + '-' + label)
    directory.mkdir()
    root = directory / 'root'
    root.mkdir()
    contents = bytes(index % 251 for index in range(size))
    (root / 'payload.bin').write_bytes(contents)
    (root / 'nested').mkdir()
    if tree:
        for index in range(4096): (root / ('d%04d' % index)).mkdir()
    if special == 'fifo': os.mkfifo(root / 'special')
    elif special == 'symlink': (root / 'special').symlink_to(root / 'payload.bin')
    elif special == 'broken-symlink': (root / 'special').symlink_to(root / 'unavailable')
    elif special == 'junction':
        created = subprocess.run(['cmd.exe', '/c', 'mklink', '/J', str(root / 'special'), str(root / 'nested')],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
        assert created.returncode == 0, created.stderr
    selected_root = str(root)
    if spelling == 'localhost': selected_root = unc(root)
    elif spelling == 'address': selected_root = unc(root, '127.0.0.1')
    elif spelling == 'upper': selected_root = unc(root).upper()
    elif spelling == 'slashes': selected_root = unc(root).replace('\\', '/')
    elif spelling == 'share':
        selected_root = '\\\\localhost\\' + str(root)[0].upper() + '$'
        second = str(root)[3:].replace('\\', '/') + '/payload.bin'
    elif spelling == 'long-unicode':
        for index in range(5):
            root = root / ('unicode-\u96ea-\U0001f431-' + str(index) + '-' + 'x' * 48)
            root.mkdir()
        (root / 'payload.bin').write_bytes(contents)
        (root / 'nested').mkdir()
        selected_root = unc(root)
        assert len(selected_root) > 260
    elif spelling != 'ordinary': raise ValueError(spelling)
    if mode == 'root-alias': second = str(root)
    if unsafe_root is not None:
        selected_root = unsafe_root(str(root), selected_root)
    argument = second if isinstance(second, bytes) else second.encode('utf-8')
    program = products / (role + '-missing-unc.' + suffix)
    argv = [str(program), selected_root.encode('utf-8').hex(), mode, argument.hex()]
    before = time.monotonic()
    process = subprocess.Popen(argv, cwd=ROOT, env={**os.environ, **sentinels}, stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, preexec_fn=limits if os.name != 'nt' else None)
    first = b''
    error = None
    try:
        if mutation:
            mailbox = queue.Queue()
            reader = threading.Thread(target=lambda: mailbox.put(process.stdout.readline()), daemon=True)
            reader.start()
            first = mailbox.get(timeout=max(0.1, 60 - (time.monotonic() - before)))
            assert first.replace(b'\r\n', b'\n') == b'ready\n'
            destination = root / ('d4095/missing.bin' if mode == 'wait-last-absence' else second)
            if mutation == 'file': destination.write_bytes(contents)
            elif mutation == 'directory': destination.mkdir()
            elif mutation == 'symlink': destination.symlink_to(root / 'payload.bin')
            elif mutation == 'broken-symlink': destination.symlink_to(root / 'unavailable')
            elif mutation == 'fifo': os.mkfifo(destination)
            elif mutation == 'junction':
                created = subprocess.run(['cmd.exe', '/c', 'mklink', '/J', str(destination), str(root / 'nested')],
                    stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
                assert created.returncode == 0, created.stderr
            elif mutation == 'disappear': destination.unlink()
            elif mutation == 'parent-custody':
                original_parent = root / 'nested'
                moved_parent = directory / 'original-parent'
                assert original_parent.resolve().is_relative_to(directory.resolve())
                assert moved_parent.resolve().is_relative_to(directory.resolve())
                captured_root = root.stat()
                if os.name == 'nt':
                    try: original_parent.rename(moved_parent)
                    except OSError as blocked:
                        assert blocked.winerror in (5, 32), blocked
                    else: raise AssertionError('retained Windows parent was renamed')
                else:
                    original_parent.rename(moved_parent)
                    original_parent.mkdir()
                    os.utime(root, ns=(captured_root.st_atime_ns, captured_root.st_mtime_ns))
            else: raise ValueError(mutation)
            stdout, stderr = process.communicate(b'x', timeout=max(0.1, 60 - (time.monotonic() - before)))
        else: stdout, stderr = process.communicate(timeout=60)
    except BaseException as caught:
        error = repr(caught)
        if process.poll() is None: process.kill()
        stdout, stderr = process.communicate(timeout=10)
    stdout = first + stdout
    record = {'role': role, 'label': label, 'mode': mode, 'argv': argv, 'exit_code': process.returncode,
        'stdout': stdout.decode('utf-8', 'replace'), 'stderr': stderr.decode('utf-8', 'replace'),
        'stdout_identity': identity(stdout), 'stderr_identity': identity(stderr),
        'error': error, 'seconds': time.monotonic() - before, 'timeout_seconds': 60,
        'address_space_bytes': None if os.name == 'nt' else 33554432,
        'descriptor_limit': None if os.name == 'nt' else 10240,
        'mutation': mutation, 'spelling': spelling, 'distinct_missing_paths':
            4096 if mode in ('absent-flat', 'absent-nested', 'absent-suffixes', 'wait-last-absence') else 0}
    records.append(record)
    (output / 'running.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    assert error is None and process.returncode == 0 and not stderr, record
    expected = ('capture %d %s\nok\n' % (size, hashlib.sha256(contents).hexdigest())).encode() if mode == 'capture' else (
        b'ready\nok\n' if mutation else b'ok\n')
    assert stdout.replace(b'\r\n', b'\n') == expected, record
    if mutation == 'parent-custody' and os.name == 'nt':
        (root / 'nested').rename(directory / 'original-parent')
        record['parent_rename_after_close_succeeded'] = True
    guard()

try:
    guard(True)
    for role in ('native', 'checked'):
        run(role, 'full-flat-missing', 'absent-flat')
        run(role, 'full-nested-missing', 'absent-nested', tree=True)
        run(role, 'full-distinct-first-absences', 'absent-suffixes')
        run(role, 'full-last-absence-appearance', 'wait-last-absence', tree=True, mutation='file')
        run(role, 'ordinary-default-missing-quota', 'missing-default-quota')
        run(role, 'missing-parent-custody', 'wait-parent-custody', mutation='parent-custody')
        run(role, 'present-file', 'kind-file', 'payload.bin')
        run(role, 'present-directory', 'kind-directory', 'nested')
        run(role, 'missing-is-not-file-identity', 'missing-file-comparison')
        run(role, 'null-kind-output', 'reject-result')
        run(role, 'present-file-disappeared', 'wait-disappeared', 'payload.bin', mutation='disappear')
        for kind in ('file', 'directory'):
            run(role, 'missing-appeared-' + kind, 'wait-absence', mutation=kind)
        for index, path in enumerate(('', '.', '..', '../payload.bin', '/payload.bin',
                'not-here//bad', 'not-here/../bad', 'not-here/bad/', b'\xc0\xaf', b'\xed\xa0\x80',
                'not-here/' + 'x' * 1024, 'not-here/' + 'x' * 8192, b'not-here/\xc0\xaf')):
            run(role, 'invalid-optional-path-%d' % index, 'reject-path', path)
        if os.name != 'nt':
            for kind in ('symlink', 'broken-symlink', 'fifo'):
                run(role, 'existing-' + kind, 'reject-path', 'special', special=kind)
                run(role, 'missing-appeared-' + kind, 'wait-absence', mutation=kind)
        else:
            run(role, 'existing-junction', 'reject-path', 'special', special='junction')
            run(role, 'missing-appeared-junction', 'wait-absence', mutation='junction')
            for spelling in ('localhost', 'address', 'upper', 'slashes', 'long-unicode'):
                run(role, 'unc-alias-' + spelling, 'root-alias', spelling=spelling)
                run(role, 'unc-capture-' + spelling, 'capture', 'payload.bin', size=65537, spelling=spelling)
            run(role, 'unc-share-only-capture', 'capture', size=65537, spelling='share')
            run(role, 'unc-full-flat-missing', 'absent-flat', spelling='localhost')
            run(role, 'unc-full-nested-missing', 'absent-nested', tree=True, spelling='localhost')
            run(role, 'unc-full-first-absences', 'absent-suffixes', spelling='localhost')
            run(role, 'unc-full-last-absence-appearance', 'wait-last-absence', tree=True,
                spelling='localhost', mutation='file')
            run(role, 'unc-missing-stage-appeared', 'wait-absence', spelling='localhost', mutation='file')
            for label, spelling in (
                ('incomplete-server', '\\\\localhost'), ('incomplete-share', '\\\\localhost\\'),
                ('device-drive', '\\\\?\\C:\\'), ('device-unc', '\\\\?\\UNC\\localhost\\C$'),
                ('device-dot', '\\\\.\\C:\\'), ('missing-share', '\\\\localhost\\cupid-absent-share-20261010')):
                run(role, 'unc-reject-' + label, 'reject-root', unsafe_root=lambda root, actual, text=spelling: text)
            run(role, 'unc-reject-file-root', 'reject-root', unsafe_root=lambda root, actual: unc(Path(root) / 'payload.bin'))
            run(role, 'unc-reject-missing-root', 'reject-root', unsafe_root=lambda root, actual: unc(Path(root) / 'unavailable'))
    left = [row for row in records if row['role'] == 'native']
    right = [row for row in records if row['role'] == 'checked']
    assert len(left) == len(right)
    for native, checked in zip(left, right):
        assert native['label'] == checked['label']
        assert native['stdout'].replace('\r\n', '\n') == checked['stdout'].replace('\r\n', '\n')
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

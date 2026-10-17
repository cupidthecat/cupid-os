from pathlib import Path
import hashlib
import json
import os
import queue
import subprocess
import tempfile
import threading
import time
import traceback

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(__file__).resolve().parent
windows = os.name == 'nt'
host, suffix = ('windows', 'exe') if windows else ('linux', 'elf')
products = BASE / ('common-products-' + host + '1')
build_bytes = (products / 'closed.json').read_bytes()
build = json.loads(build_bytes)
assert build['status'] == 'pass'
output = BASE / ('common-input-request-binding-contracts-' + host + '1')
assert not output.exists()
output.mkdir()
trees = output / 'trees' if windows else Path(tempfile.mkdtemp(prefix='cupid-input-request-', dir='/var/tmp'))
trees.mkdir(exist_ok=True)
identity = lambda data: {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
records = []
state = {'schema': 'cupid.input-request-binding-contracts.v1', 'status': 'running', 'host': host,
    'build': identity(build_bytes), 'runner': identity(Path(__file__).read_bytes()), 'records': records,
    'producer_sentinels': build['producer_sentinels'], 'timeout_seconds': 60,
    'linux_address_space_bytes': None if windows else 33554432,
    'linux_descriptor_limit': None if windows else 10240}
started = time.monotonic()

def guard():
    assert (products / 'closed.json').read_bytes() == build_bytes
    assert identity((BASE / 'common-source' / 'input_request_binding_contract.cc').read_bytes()) == build['sources']['input_request_binding_contract.cc']
    for name, facts in build['sources'].items(): assert identity((BASE / 'common-source' / name).read_bytes()) == facts
    for name, facts in build['normal_source_inputs'].items(): assert identity((ROOT / name).read_bytes()) == facts
    for role in ('native', 'checked'):
        assert identity((products / (role + '-input-request-binding.' + suffix)).read_bytes()) == build[role + '_images']['input-request-binding']

def limits():
    import resource
    resource.setrlimit(resource.RLIMIT_AS, (33554432, 33554432))
    resource.setrlimit(resource.RLIMIT_NOFILE, (10240, 10240))

def run(role, namespace, flag, label, mode='check', request='payload.bin', mutation=None, expected=0):
    guard()
    name = role + '-' + namespace + '-' + str(flag) + '-' + label
    tree = trees / (name + '-\u03b4-\u732b')
    working, other, alias = tree / 'working', tree / 'other', tree / 'alias'
    working.mkdir(parents=True)
    other.mkdir()
    payload = working / 'payload.bin'
    payload.write_bytes(b'original-file')
    (working / 'different.bin').write_bytes(b'different-file')
    (working / 'directory').mkdir()
    os.link(payload, other / 'payload.bin')
    if 'alias' in label: os.symlink(working, alias, target_is_directory=True)
    root_text = str(working)
    if namespace == 'unc': root_text = '\\\\localhost\\C$' + root_text[2:]
    argv = [str(products / (role + '-input-request-binding.' + suffix)), mode,
        root_text.encode('utf-8').hex(), request.encode('utf-8').hex(), str(flag)]
    before = time.monotonic()
    process = subprocess.Popen(argv, cwd=ROOT, env={**os.environ, **build['producer_sentinels']},
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        preexec_fn=None if windows else limits)
    prefix = b''
    try:
        if mutation:
            packets = queue.Queue()
            threading.Thread(target=lambda: packets.put(process.stdout.readline()), daemon=True).start()
            prefix = packets.get(timeout=max(.001, 60 - (time.monotonic() - before)))
            assert prefix.rstrip(b'\r\n') == b'ready', prefix
            if mutation == 'metadata': payload.write_bytes(b'changed-file-with-a-different-size')
            elif mutation == 'sibling': (working / 'new-sibling.bin').write_bytes(b'new-sibling')
            elif mutation == 'alias-rebind':
                assert alias.parent.resolve() == tree.resolve() and alias.resolve() == working.resolve()
                alias.unlink()
                os.symlink(other, alias, target_is_directory=True)
                assert os.path.samefile(payload, other / 'payload.bin')
            else:
                payload.unlink()
                if mutation == 'replace': payload.write_bytes(b'original-file')
                elif mutation == 'symlink': os.symlink(other / 'payload.bin', payload)
                elif mutation == 'directory': payload.mkdir()
                elif mutation == 'fifo': os.mkfifo(payload)
                else: assert mutation == 'missing'
        stdout, stderr = process.communicate(input=b'x\n', timeout=max(.001, 60 - (time.monotonic() - before)))
        row = {'case': name, 'role': role, 'namespace': namespace, 'parent_identity_only': flag,
            'mode': mode, 'mutation': mutation, 'argv': argv, 'expected_exit': expected,
            'exit_code': process.returncode, 'seconds': time.monotonic() - before,
            'stdout': (prefix + stdout).decode('ascii'), 'stderr': stderr.decode('utf-8'),
            'timeout_seconds': 60, 'linux_address_space_bytes': None if windows else 33554432,
            'linux_descriptor_limit': None if windows else 10240}
        records.append(row)
        assert process.returncode == expected and stderr == b'' and stdout.replace(b'\r\n', b'\n') == (b'accept\n' if expected == 0 else b'reject\n'), row
        guard()
    except BaseException:
        process.kill()
        stdout, stderr = process.communicate()
        (output / (name + '-failure-stdout.bin')).write_bytes(prefix + stdout)
        (output / (name + '-failure-stderr.bin')).write_bytes(stderr)
        raise
    finally:
        (output / 'running.json').write_text(json.dumps(state, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')

try:
    for role in ('native', 'checked'):
        for namespace in ('drive', 'unc') if windows else ('posix',):
            for flag in (0, 1):
                run(role, namespace, flag, 'unchanged')
                run(role, namespace, flag, 'quota-two', mode='quota-two')
                run(role, namespace, flag, 'parent-alias', request='../alias/payload.bin')
                run(role, namespace, flag, 'new-sibling', mode='hold', mutation='sibling')
                for mode in ('null-observer', 'null-logical', 'null-request', 'null-physical',
                             'relative-request', 'trailing-separator', 'wrong-physical', 'bad-utf8',
                             'bad-flag', 'unobserved', 'malformed-logical', 'directory', 'missing'):
                    run(role, namespace, flag, mode, mode=mode, expected=1)
                for mutation in ('metadata', 'missing', 'replace', 'symlink', 'directory') + (() if windows else ('fifo',)):
                    run(role, namespace, flag, 'changed-' + mutation, mode='hold', mutation=mutation, expected=1)
                run(role, namespace, flag, 'alias-rebind-hardlink', mode='hold',
                    request='../alias/payload.bin', mutation='alias-rebind', expected=1)
    state.update(status='pass', calls=len(records), positive_calls=sum(r['expected_exit'] == 0 for r in records),
        negative_calls=sum(r['expected_exit'] == 1 for r in records),
        repeated_binding_checks=9 * sum(r['expected_exit'] == 0 for r in records),
        quota_exhaustion_controls=sum(r['mode'] == 'quota-two' for r in records))
except BaseException as error:
    state.update(status='fail', error=repr(error))
    traceback.print_exc()
state['seconds'] = time.monotonic() - started
(output / 'closed.json').write_text(json.dumps(state, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({k: state.get(k) for k in ('status', 'calls', 'positive_calls', 'negative_calls', 'seconds', 'error')}))
raise SystemExit(0 if state['status'] == 'pass' else 1)

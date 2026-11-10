from pathlib import Path
import base64
import hashlib
import json
import os
import queue
import subprocess
import tempfile
import threading
import time
import traceback

BASE = Path(__file__).resolve().parent
ROOT = BASE.parents[1]
windows = os.name == 'nt'
host, suffix = ('windows', 'exe') if windows else ('linux', 'elf')
identity = lambda data: {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
products = BASE / ('common-file-request-group-products-' + host + '1')
build_bytes = (products / 'closed.json').read_bytes()
build = json.loads(build_bytes)
assert build['status'] == 'pass'
output = BASE / ('common-file-request-group-contracts-' + host + '1')
assert not output.exists()
output.mkdir()
trees = output / 'trees' if windows else Path(tempfile.mkdtemp(prefix='cupid-request-groups-', dir='/var/tmp'))
trees.mkdir(exist_ok=True)
cases = [('positive', 1), ('positive', 512), ('positive', 4096), ('mixed', 12),
         ('duplicate', 3), ('repeat', 3), ('zero', 3), ('no-callback', 3), ('single-read', 3),
         ('new-borrow', 3), ('borrowed-boundary', 3), ('parent-identity', 3),
         ('sink-fail', 3), ('reenter', 3), ('close', 3), ('recursive', 3),
         ('path-reenter', 3), ('stream-reenter', 3), ('null-requests', 3), ('overflow', 3),
         ('null-logical', 3), ('unknown-logical', 3), ('traversal', 3),
         ('null-request', 3), ('null-physical', 3), ('relative-request', 3),
         ('relative-physical', 3), ('bad-mode', 3), ('wrong-leaf', 3),
         ('mutate-current', 3), ('parent-retarget', 3), ('parent-metadata', 3),
         ('root-retarget', 3), ('payload-restore', 1), ('earlier-payload', 3)]
positive = {mode for mode, _ in cases[:12]}
mutations = {'mutate-current', 'parent-retarget', 'parent-metadata', 'parent-identity',
             'root-retarget', 'payload-restore', 'earlier-payload'}
state = {'schema': 'cupid.file-request-group-contracts.v1', 'status': 'running', 'host': host,
         'build': identity(build_bytes), 'runner': identity(Path(__file__).read_bytes()),
         'fixture': build['fixture'], 'producer_sentinels': build['producer_sentinels'],
         'case_plan': [{'mode': mode, 'count': count} for mode, count in cases],
         'timeout_seconds': 60, 'linux_address_space_bytes': 33554432,
         'linux_descriptor_limit': 10240, 'records': [], 'qualification': False,
         'scope': 'Private original-request metadata groups with complete caller validation before and after.'}
started = time.monotonic()

def guard():
    assert (products / 'closed.json').read_bytes() == build_bytes
    assert identity((BASE / 'file_request_group_contract.cc').read_bytes()) == build['fixture']
    for name, facts in build['sources'].items():
        assert identity((BASE / 'common-source' / name).read_bytes()) == facts, name
    for group in ['normal_source_inputs', 'projected_files']:
        for name, facts in build[group].items():
            assert identity((ROOT / name).read_bytes()) == facts, name
    for role in ['native', 'checked']:
        assert identity((products / (role + '-file-request-group.' + suffix)).read_bytes()) == build[role + '_image']

def limits():
    import resource
    resource.setrlimit(resource.RLIMIT_AS, (33554432, 33554432))
    resource.setrlimit(resource.RLIMIT_NOFILE, (10240, 10240))

try:
    for role in ['native', 'checked']:
        for namespace in (['drive', 'unc'] if windows else ['posix']):
            for mode, count in cases:
                guard()
                folder = trees / (role + '-' + namespace + '-' + mode + '-' + str(count) + '-\u03b4-\u732b')
                physical = folder / 'physical'
                data = physical / 'data flat \u03b4-\u732b'
                data.mkdir(parents=True)
                for index in range(count):
                    parent = physical / (['z parent', 'a parent', 'a parent extra'][index % 3]) if mode == 'mixed' else data
                    parent.mkdir(exist_ok=True)
                    (parent / ('input-%04u.bin' % index)).write_bytes(('payload-%04u' % index).encode('ascii'))
                (physical / 'source.bin').write_bytes(b'checked-source')
                (physical / 'public.bin').write_bytes(b'previous-public-output')
                root = physical
                if mode == 'root-retarget':
                    root = folder / 'alias'; other = folder / 'other'; other.mkdir()
                    os.symlink(physical, root, target_is_directory=True)
                if mode == 'parent-retarget':
                    other = folder / 'replacement-data'; other.mkdir()
                    alias = physical / 'alias parent'; os.symlink(data, alias, target_is_directory=True)
                    for index in range(count):
                        (other / ('input-%04u.bin' % index)).write_bytes(('payload-%04u' % index).encode('ascii'))
                requested = root if namespace != 'unc' else Path('//localhost/C$' + root.as_posix()[2:])
                argv = [str(products / (role + '-file-request-group.' + suffix)), str(requested).encode('utf-8').hex(),
                        str(count), mode, str(physical).encode('utf-8').hex()]
                before = time.monotonic()
                process = subprocess.Popen(argv, cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE, env={**os.environ, **state['producer_sentinels']},
                    preexec_fn=None if windows else limits)
                lines, markers, incoming = [], [], queue.Queue()
                def capture():
                    for line in iter(process.stdout.readline, b''):
                        lines.append(line); text = line.rstrip(b'\r\n').decode('ascii')
                        markers.append({'text': text, 'seconds': time.monotonic() - before}); incoming.put(text)
                    incoming.put(None)
                worker = threading.Thread(target=capture, daemon=True); worker.start()
                mutation = None
                if mode in mutations:
                    marker = incoming.get(timeout=max(0.01, 60 - (time.monotonic() - before)))
                    assert isinstance(marker, str) and marker.startswith('ready '), marker
                    index = int(marker.split()[1]); assert 0 <= index < count
                    if mode in ['mutate-current', 'payload-restore', 'earlier-payload']:
                        target = data / ('input-%04u.bin' % (index if mode == 'mutate-current' else 0))
                        original = target.read_bytes(); previous = target.stat()
                        changed = original + b'-changed' if mode == 'mutate-current' else bytes((byte + 1) % 128 for byte in original)
                        target.write_bytes(changed)
                        if mode != 'mutate-current':
                            os.utime(target, ns=(previous.st_atime_ns, previous.st_mtime_ns))
                            assert target.stat().st_size == previous.st_size and target.stat().st_mtime_ns == previous.st_mtime_ns
                        mutation = {'kind': mode, 'original': identity(original), 'changed': identity(target.read_bytes()),
                                    'mtime_ns_before': previous.st_mtime_ns, 'mtime_ns_after': target.stat().st_mtime_ns}
                    elif mode in ['parent-metadata', 'parent-identity']:
                        previous = data.stat(); (data / 'new-unobserved-sibling.bin').write_bytes(b'sibling')
                        assert data.stat().st_mtime_ns != previous.st_mtime_ns
                        mutation = {'kind': mode, 'mtime_ns_before': previous.st_mtime_ns, 'mtime_ns_after': data.stat().st_mtime_ns}
                    elif mode == 'parent-retarget':
                        assert alias.is_symlink(); alias.unlink(); os.symlink(other, alias, target_is_directory=True)
                        mutation = {'kind': 'original-parent-alias-retarget', 'before': str(data), 'after': str(other)}
                    else:
                        assert root.is_symlink(); root.unlink(); os.symlink(other, root, target_is_directory=True)
                        mutation = {'kind': 'original-root-retarget', 'before': str(physical), 'after': str(other)}
                    process.stdin.write(b'x\n'); process.stdin.flush()
                timed_out = False
                try: process.wait(timeout=max(0.01, 60 - (time.monotonic() - before)))
                except subprocess.TimeoutExpired:
                    timed_out = True; process.kill(); process.wait()
                worker.join(timeout=2); assert not worker.is_alive()
                stdout, stderr = b''.join(lines), process.stderr.read()
                expected = 'accept' if mode in positive else 'reject'
                row = {'role': role, 'namespace': namespace, 'mode': mode, 'count': count, 'argv': argv,
                       'exit_code': process.returncode, 'seconds': time.monotonic() - before, 'timed_out': timed_out,
                       'timeout_seconds': 60, 'linux_address_space_bytes': None if windows else 33554432,
                       'linux_descriptor_limit': None if windows else 10240, 'markers': markers,
                       'expected_final_line': expected, 'stdout': identity(stdout), 'stderr': identity(stderr),
                       'raw_stdout_base64': base64.b64encode(stdout).decode('ascii'),
                       'raw_stderr_base64': base64.b64encode(stderr).decode('ascii'), 'mutation': mutation,
                       'public_output_before': identity(b'previous-public-output'),
                       'public_output_after': identity((physical / 'public.bin').read_bytes())}
                label = role + '-' + namespace + '-' + mode + '-' + str(count)
                for stream, raw in [('stdout', stdout), ('stderr', stderr)]:
                    (output / (label + '-' + stream + '.bin')).write_bytes(raw)
                state['records'].append(row)
                (output / 'running.json').write_text(json.dumps(state, indent=2, sort_keys=True) + '\n', encoding='utf-8')
                assert not timed_out and process.returncode == 0 and not stderr and markers[-1]['text'] == expected, row
                assert row['public_output_before'] == row['public_output_after']
                if mode in ['payload-restore', 'earlier-payload']:
                    assert 'group=1 frame=0' in markers[-2]['text']
                if mode in ['null-requests', 'overflow', 'null-logical', 'unknown-logical', 'traversal',
                            'null-request', 'null-physical', 'relative-request', 'relative-physical', 'bad-mode', 'wrong-leaf']:
                    assert 'callbacks=0' in markers[-2]['text']
                guard()
                print(json.dumps({key: row[key] for key in ['role', 'namespace', 'mode', 'count', 'seconds']}), flush=True)
    state.update(status='pass', calls=len(state['records']),
                 positive_calls=sum(row['expected_final_line'] == 'accept' for row in state['records']),
                 negative_calls=sum(row['expected_final_line'] == 'reject' for row in state['records']))
except BaseException as error:
    state.update(status='fail', error=repr(error)); traceback.print_exc()
    if 'process' in globals() and process.poll() is None: process.kill(); process.wait()
state['seconds'] = time.monotonic() - started
(output / 'closed.json').write_text(json.dumps(state, indent=2, sort_keys=True) + '\n', encoding='utf-8')
print(json.dumps({key: state.get(key) for key in ['status', 'calls', 'positive_calls', 'negative_calls', 'seconds', 'error']}))
raise SystemExit(0 if state['status'] == 'pass' else 1)

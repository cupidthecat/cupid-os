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

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(__file__).resolve().parent
windows = os.name == 'nt'
host, suffix = ('windows', 'exe') if windows else ('linux', 'elf')
identity = lambda data: {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
products = BASE / ('common-selection-path-group-products-' + host + '1')
build_bytes = (products / 'closed.json').read_bytes()
build = json.loads(build_bytes)
assert build['status'] == 'pass'
output = BASE / ('common-selection-path-group-contracts-' + host + '1')
assert not output.exists()
output.mkdir()
tree_base = output / 'trees' if windows else Path(tempfile.mkdtemp(prefix='cupid-stream-groups-', dir='/var/tmp'))
tree_base.mkdir(exist_ok=True)
cases = [('positive', 1), ('positive', 512), ('positive', 4096), ('repeat', 3), ('zero', 3), ('no-callback', 3), ('mixed', 3), ('new-borrow', 3), ('sink-fail', 3), ('reenter', 3), ('close', 3), ('recursive', 3), ('path-reenter', 3), ('stream-reenter', 3), ('null-requests', 3), ('null-results', 3), ('overflow', 3), ('null-logical', 3), ('invalid-utf8', 3), ('traversal', 3), ('trailing-slash', 3), ('linked', 3), ('linked-parent', 3), ('quota', 512), ('mutate', 3), ('root-retarget', 3), ('previous-digest', 3), ('source-root-metadata', 3), ('source-root-sibling', 3), ('ordinary-root-metadata', 3), ('complete-root', 3), ('selection-borrow', 3)]
state = {'schema': 'cupid.optional-selection-path-group-contracts.v1', 'status': 'running', 'host': host,
         'build': identity(build_bytes), 'runner': identity(Path(__file__).read_bytes()),
         'fixture': build['fixture'], 'case_plan': [dict(mode=mode, count=count) for mode, count in cases],
         'timeout_seconds': 60, 'linux_address_space_bytes': 33554432, 'linux_descriptor_limit': 10240,
         'producer_sentinels': build['producer_sentinels'], 'records': [],
         'scope': 'Private optional selection group. Ordinary root metadata controls remain strict; complete owner compatibility remains unqualified.'}
started = time.monotonic()

def guard():
    assert (products / 'closed.json').read_bytes() == build_bytes
    for name, facts in build['sources'].items():
        assert identity((BASE / 'common-source' / name).read_bytes()) == facts, name
    for group in ('normal_source_inputs', 'projected_files'):
        for rel, facts in build[group].items():
            assert identity((ROOT / rel).read_bytes()) == facts, rel
    assert identity((BASE / 'selection_path_group_contract.cc').read_bytes()) == build['fixture']
    for role in ('native', 'checked'):
        assert identity((products / (role + '-selection-path-group.' + suffix)).read_bytes()) == build[role + '_image']

def limits():
    import resource
    resource.setrlimit(resource.RLIMIT_AS, (33554432, 33554432))
    resource.setrlimit(resource.RLIMIT_NOFILE, (10240, 10240))

try:
    for role in ('native', 'checked'):
        for namespace in (('drive', 'unc') if windows else ('posix',)):
            for mode, count in cases:
                guard()
                folder = tree_base / (role + '-' + namespace + '-' + mode + '-' + str(count) + '-\u03b4-\u732b')
                physical = folder / 'physical'
                data_folder = physical / 'data flat \u03b4-\u732b'
                data_folder.mkdir(parents=True)
                for index in range(count):
                    payload = bytes((byte + index * 13) % 251 for byte in range(131083)) if mode in ('chunked', 'chunked-limit') else b'' if mode == 'empty' else ('payload-%04u' % index).encode('ascii')
                    (data_folder / ('input-%04u.bin' % index)).write_bytes(payload)
                if mode == 'linked': os.symlink(data_folder / 'input-0000.bin', physical / 'linked.bin')
                if mode == 'linked-parent': os.symlink(data_folder, physical / 'linked-directory', target_is_directory=True)
                (physical / 'source.bin').write_bytes(b'checked-source')
                (physical / 'public.bin').write_bytes(b'previous-public-output')
                root = physical
                if mode == 'root-retarget':
                    other = folder / 'other'; other.mkdir()
                    root = folder / 'alias'
                    os.symlink(physical, root, target_is_directory=True)
                actual_root = root if namespace != 'unc' else Path('//localhost/C$' + root.as_posix()[2:])
                argv = [str(products / (role + '-selection-path-group.' + suffix)), str(actual_root).encode('utf-8').hex(), str(count), mode, str(physical).encode('utf-8').hex()]
                before = time.monotonic()
                process = subprocess.Popen(argv, cwd=ROOT, stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                    env={**os.environ, **state['producer_sentinels']}, preexec_fn=None if windows else limits)
                lines, markers = [], []
                incoming = queue.Queue()
                def capture():
                    for line in iter(process.stdout.readline, b''):
                        lines.append(line)
                        text = line.rstrip(b'\r\n').decode('ascii')
                        markers.append({'text': text, 'seconds': time.monotonic() - before})
                        incoming.put(text)
                    incoming.put(None)
                worker = threading.Thread(target=capture, daemon=True); worker.start()
                mutation = None
                if mode in ('mutate', 'root-retarget', 'previous-digest', 'source-root-metadata', 'source-root-sibling', 'ordinary-root-metadata'):
                    marker = incoming.get(timeout=max(0.01, 60 - (time.monotonic() - before)))
                    assert marker == 'ready', marker
                    if mode in ('mutate', 'previous-digest'):
                        target = data_folder / 'input-0000.bin'
                        stat_before = target.stat()
                        original = target.read_bytes()
                        if mode == 'previous-digest':
                            target.write_bytes(b'changed-0000')
                            os.utime(target, ns=(stat_before.st_atime_ns, stat_before.st_mtime_ns))
                            after = target.stat()
                            assert target.read_bytes() != original and after.st_size == stat_before.st_size and after.st_mtime_ns == stat_before.st_mtime_ns
                            mutation = {'kind': 'same-size-restored-mtime', 'original': identity(original), 'changed': identity(target.read_bytes()),
                                        'mtime_ns_before': stat_before.st_mtime_ns, 'mtime_ns_after': after.st_mtime_ns}
                        else:
                            target.write_bytes(original + b'-changed')
                            assert target.stat().st_size != stat_before.st_size
                            mutation = {'kind': 'earlier-file-size-change', 'original': identity(original), 'changed': identity(target.read_bytes())}
                    elif mode in ('source-root-metadata', 'source-root-sibling', 'ordinary-root-metadata'):
                        before_stat = physical.stat()
                        if mode == 'source-root-sibling':
                            (physical / 'uncaptured-sibling.bin').write_bytes(b'uncaptured-sibling')
                        os.utime(physical, ns=(before_stat.st_atime_ns, before_stat.st_mtime_ns + 3000000000))
                        after_stat = physical.stat()
                        assert after_stat.st_mtime_ns != before_stat.st_mtime_ns
                        mutation = {'kind': 'uncaptured-root-sibling' if mode == 'source-root-sibling' else 'root-metadata',
                                    'mtime_ns_before': before_stat.st_mtime_ns, 'mtime_ns_after': after_stat.st_mtime_ns}
                    else:
                        assert root.is_symlink()
                        root.unlink()
                        os.symlink(other, root, target_is_directory=True)
                        mutation = {'kind': 'original-root-retarget', 'before': str(physical), 'after': str(other)}
                    process.stdin.write(b'x\n'); process.stdin.flush()
                timed_out = False
                try:
                    process.wait(timeout=max(0.01, 60 - (time.monotonic() - before)))
                except subprocess.TimeoutExpired:
                    timed_out = True; process.kill(); process.wait()
                worker.join(timeout=2); assert not worker.is_alive()
                raw, stderr = b''.join(lines), process.stderr.read()
                expected = 'accept' if mode in ('positive', 'repeat', 'zero', 'no-callback', 'mixed', 'new-borrow', 'selection-borrow', 'source-root-metadata', 'source-root-sibling') else 'reject'
                row = {'role': role, 'namespace': namespace, 'mode': mode, 'stage_count': count,
                       'argv': argv, 'publication_root': str(physical), 'publication_namespace': 'drive' if windows else 'posix', 'exit_code': process.returncode, 'expected_exit': 0, 'expected_final_line': expected,
                       'seconds': time.monotonic() - before, 'timed_out': timed_out, 'markers': markers,
                       'timeout_seconds': 60, 'linux_address_space_bytes': 33554432 if not windows else None,
                       'linux_descriptor_limit': 10240 if not windows else None,
                       'stdout': identity(raw), 'stderr': identity(stderr),
                       'raw_stdout_base64': base64.b64encode(raw).decode('ascii'),
                       'raw_stderr_base64': base64.b64encode(stderr).decode('ascii'), 'mutation': mutation,
                       'source_payload_bytes': count * 12, 'payload_streaming': mode == 'previous-digest',
                       'previous_digest_limit_bytes': 64 if mode == 'previous-digest' else None,
                       'public_output_before': identity(b'previous-public-output'),
                       'public_output_after': identity((physical / 'public.bin').read_bytes())}
                label = role + '-' + namespace + '-' + mode + '-' + str(count)
                for stream, data in [('stdout', raw), ('stderr', stderr)]: (output / (label + '-' + stream + '.bin')).write_bytes(data)
                state['records'].append(row)
                (output / 'running.json').write_text(json.dumps(state, indent=2, sort_keys=True) + '\n', encoding='utf-8')
                assert not timed_out and process.returncode == 0 and not stderr and markers[-1]['text'] == expected, row
                assert row['public_output_before'] == row['public_output_after']
                guard()
                print(json.dumps({k: row[k] for k in ['role', 'namespace', 'mode', 'stage_count', 'seconds']}), flush=True)
    state.update(status='pass', calls=len(state['records']), positive_calls=sum(r['expected_final_line'] == 'accept' for r in state['records']),
                 negative_calls=sum(r['expected_final_line'] == 'reject' for r in state['records']))
except BaseException as error:
    state.update(status='fail', error=repr(error)); traceback.print_exc()
    if 'process' in globals() and process.poll() is None:
        process.kill(); process.wait()
state['seconds'] = time.monotonic() - started
(output / 'closed.json').write_text(json.dumps(state, indent=2, sort_keys=True) + '\n', encoding='utf-8')
print(json.dumps({key: state.get(key) for key in ['status', 'calls', 'positive_calls', 'negative_calls', 'seconds', 'error']}))
raise SystemExit(0 if state['status'] == 'pass' else 1)

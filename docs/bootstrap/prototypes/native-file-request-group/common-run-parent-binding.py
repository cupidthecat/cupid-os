from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile
import time
import traceback

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(__file__).resolve().parent
windows = os.name == 'nt'
host, suffix = ('windows', 'exe') if windows else ('linux', 'elf')
products = BASE / ('common-parent-binding-products-' + host + '1')
build_bytes = (products / 'closed.json').read_bytes()
build = json.loads(build_bytes)
assert build['status'] == 'pass'
output = BASE / ('common-parent-binding-contracts-' + host + '1')
assert not output.exists()
output.mkdir()
trees = output / 'trees' if windows else Path(tempfile.mkdtemp(prefix='cupid-parent-binding-', dir='/var/tmp'))
trees.mkdir(exist_ok=True)
identity = lambda data: {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
records = []
state = {'schema': 'cupid.parent-binding-contracts.v1', 'status': 'running', 'host': host,
    'build': identity(build_bytes), 'runner': identity(Path(__file__).read_bytes()), 'records': records,
    'producer_sentinels': build['producer_sentinels'], 'timeout_seconds': 60,
    'linux_address_space_bytes': None if windows else 33554432,
    'linux_descriptor_limit': None if windows else 10240}
started = time.monotonic()

def guard():
    assert (products / 'closed.json').read_bytes() == build_bytes
    assert identity((BASE / 'source_parent_binding_contract.cc').read_bytes()) == build['fixture']
    for name, facts in build['sources'].items(): assert identity((BASE / 'common-source' / name).read_bytes()) == facts
    for name, facts in build['normal_source_inputs'].items(): assert identity((ROOT / name).read_bytes()) == facts
    for role in ('native', 'checked'):
        assert identity((products / (role + '-parent-binding.' + suffix)).read_bytes()) == build[role + '_image']

def limits():
    import resource
    resource.setrlimit(resource.RLIMIT_AS, (33554432, 33554432))
    resource.setrlimit(resource.RLIMIT_NOFILE, (10240, 10240))

try:
    for role in ('native', 'checked'):
        for namespace in ('drive', 'unc') if windows else ('posix',):
            root = trees / (role + '-' + namespace + '-\u03b4-\u732b')
            (root / 'a').mkdir(parents=True)
            (root / 'b').mkdir()
            (root / 'a/one.bin').write_bytes(b'one')
            (root / 'a/two.bin').write_bytes(b'two')
            os.link(root / 'a/one.bin', root / 'b/hardlink.bin')
            (root / 'b/other.bin').write_bytes(b'other')
            request = str(root)
            if namespace == 'unc': request = '\\\\localhost\\C$' + request[2:]
            for label, left, right, same, reject in [
                ('same-file', 'a/one.bin', 'a/one.bin', 1, 0),
                ('same-directory', 'a/one.bin', 'a/two.bin', 1, 0),
                ('hardlinks-different-directories', 'a/one.bin', 'b/hardlink.bin', 0, 0),
                ('different-directories', 'a/one.bin', 'b/other.bin', 0, 0),
                ('unobserved-clears-poisons', 'a/one.bin', 'a/two.bin', 0, 1),
                ('null-result-poisons', 'a/one.bin', 'a/two.bin', 0, 2)]:
                guard()
                argv = [str(products / (role + '-parent-binding.' + suffix)), 'parent',
                    request.encode('utf-8').hex(), request.encode('utf-8').hex(),
                    left.encode('ascii').hex(), right.encode('ascii').hex(), str(same), str(reject)]
                before = time.monotonic()
                result = subprocess.run(argv, cwd=ROOT, env={**os.environ, **build['producer_sentinels']},
                    capture_output=True, timeout=60, preexec_fn=None if windows else limits)
                row = {'role': role, 'namespace': namespace, 'label': label, 'argv': argv,
                    'expected_same': same, 'rejection_control': reject, 'expected_exit': 0,
                    'exit_code': result.returncode, 'stdout': result.stdout.decode('ascii'),
                    'stderr': result.stderr.decode('utf-8'), 'seconds': time.monotonic() - before,
                    'timeout_seconds': 60, 'linux_address_space_bytes': None if windows else 33554432,
                    'linux_descriptor_limit': None if windows else 10240}
                records.append(row)
                assert result.returncode == 0 and result.stdout.replace(b'\r\n', b'\n') == b'accept\n' and result.stderr == b'', row
                guard()
                (output / 'running.json').write_text(json.dumps(state, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    state.update(status='pass', calls=len(records), successful_comparison_checks=sum(not r['rejection_control'] for r in records),
        rejection_clear_poison_checks=sum(bool(r['rejection_control']) for r in records))
except BaseException as error:
    state.update(status='fail', error=repr(error))
    traceback.print_exc()
state['seconds'] = time.monotonic() - started
(output / 'closed.json').write_text(json.dumps(state, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({k: state.get(k) for k in ('status', 'calls', 'rejection_clear_poison_checks', 'seconds', 'error')}))
raise SystemExit(0 if state['status'] == 'pass' else 1)

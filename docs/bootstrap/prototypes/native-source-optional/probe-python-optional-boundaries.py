from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
BASE = Path(__file__).resolve().parent
sys.path.insert(0, str(BASE))
from consumer_audit_common import read, identity
sys.path.insert(0, str(ROOT))
from tools import hostbuild

windows = os.name == 'nt'
host, suffix = ('windows', 'exe') if windows else ('linux', 'elf')
products = BASE / ('products-' + host + '1')
build_bytes = read(products / 'closed.json')
build = json.loads(build_bytes)
assert build['status'] == 'pass'
from replay_guard import make_guard
guard = make_guard(ROOT, BASE)
guard()
output = BASE / ('python-optional-boundaries-' + host + '1')
assert not output.exists()
output.mkdir()
tree = output / 'tree' if windows else Path(tempfile.mkdtemp(prefix='cupid-optional-boundaries-', dir='/var/tmp'))
tree.mkdir(exist_ok=True)
(tree / 'parent-file').write_bytes(b'regular parent')
target = tree / 'removed-target'
target.mkdir()
alias = tree / 'broken-parent'
if windows:
    result = subprocess.run(['cmd.exe', '/d', '/c', 'mklink', '/J', str(alias), str(target)], capture_output=True, timeout=10)
    assert result.returncode == 0, result.stderr
else:
    alias.symlink_to(target, target_is_directory=True)
assert target.resolve().is_relative_to(tree.resolve())
target.rmdir()
assert not alias.exists()
sentinels = {name: 'forbidden-host-' + name for name in ('CC', 'CXX', 'CPP', 'AS', 'AR', 'LD', 'NM', 'OBJCOPY', 'NASM')}
records = []


def limits():
    import resource
    resource.setrlimit(resource.RLIMIT_AS, (33554432, 33554432))
    resource.setrlimit(resource.RLIMIT_NOFILE, (10240, 10240))


def normalized(path):
    return str(path).replace('\\', '/') if windows else str(path)


for label in ('parent-file', 'broken-parent'):
    guard()
    request = label + '/payload.bin'
    requested = hostbuild._disk_absolute(tree / request)
    try:
        linked = hostbuild._disk_is_link_or_junction(requested, 'stage input')
        assert not linked
        classification = 'present' if requested.exists() else 'missing'
        python_error = None
    except hostbuild.DiskImageError as error:
        classification, python_error = 'reject', str(error)
    expected = 'reject' if label == 'parent-file' and not windows else 'missing'
    assert classification == expected, (host, label, classification)
    for role in ('native', 'checked'):
        program = products / (role + '-source-optional.' + suffix)
        facts = build[role + '_optional_image']
        assert identity(read(program)) == facts
        values = (str(tree), request, normalized(tree / request), str(requested), normalized(tree), request)
        argv = [str(program), 'optional', *(value.encode('utf-8').hex() for value in values), 'none']
        result = subprocess.run(argv, cwd=ROOT, env={**os.environ, **sentinels}, capture_output=True,
                                timeout=60, preexec_fn=None if windows else limits)
        assert result.returncode == 2 and result.stdout == b'reject' and result.stderr == b'', (role, label, result)
        assert identity(read(program)) == facts
        records.append({'role': role, 'label': label, 'argv': argv, 'python_classification': classification,
            'python_error': python_error, 'native_owner_classification': 'reject', 'exit_code': result.returncode,
            'stdout': result.stdout.decode('ascii'), 'stderr': result.stderr.decode('ascii'),
            'timeout_seconds': 60, 'linux_address_space_bytes': None if windows else 33554432,
            'linux_descriptor_limit': None if windows else 10240})
guard()
assert read(products / 'closed.json') == build_bytes
receipt = {'schema': 'cupid.native-source-optional-python-boundaries.v1', 'status': 'pass', 'host': host,
    'build': identity(build_bytes), 'runner': identity(read(Path(__file__))), 'producer_sentinels': sentinels,
    'records': records, 'calls': len(records),
    'finding': 'Linux Python rejects a regular-file parent during final inspection; Windows Python classifies its child as missing. Both Python hosts classify a child of a broken parent alias as missing. The held owner rejects both parent forms. Complete native image integration must resolve the Windows file-parent and both broken-parent compatibility differences.'}
(output / 'closed.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({'status': 'pass', 'host': host, 'calls': len(records), 'python_file_parent': 'missing' if windows else 'reject',
                  'python_broken_parent': 'missing', 'owner_broken_parent': 'reject'}))

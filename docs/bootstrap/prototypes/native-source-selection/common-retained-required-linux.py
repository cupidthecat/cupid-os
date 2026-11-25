from pathlib import Path
import json
import os
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
sys.path.insert(0, str(ROOT))
from tools import hostbuild

windows = os.name == 'nt'
host, suffix = ('windows', 'exe') if windows else ('linux', 'elf')
products = BASE / ('common-products-' + host + '1')
build_bytes = read(products / 'closed.json')
build = json.loads(build_bytes)
assert build['status'] == 'pass'
output = BASE / ('common-retained-required-contracts-' + host + '1')
assert not output.exists()
output.mkdir()
trees = output / 'trees' if windows else Path(tempfile.mkdtemp(prefix='cupid-source-file-', dir='/var/tmp'))
trees.mkdir(exist_ok=True)
sentinels = {name: 'forbidden-host-' + name for name in ('CC', 'CXX', 'CPP', 'AS', 'AR', 'LD', 'NM', 'OBJCOPY', 'NASM')}
environment = {**os.environ, **sentinels}
source_proof = json.loads(read(BASE / 'source-controls.json'))
application_bytes = read(ROOT / 'build/native-input-sharing-20261010/preceding-checkpoint-controls.json')
application = json.loads(application_bytes)
records, oracles = [], []
receipt = {'schema': 'cupid.native-source-file-contracts.v1', 'status': 'running', 'host': host,
    'records': records, 'actual_python_oracles': oracles, 'tree_root': str(trees),
    'build': identity(build_bytes), 'runner': identity(read(Path(__file__))), 'producer_sentinels': sentinels,
    'timeout_seconds': 60, 'linux_address_space_bytes': 33554432, 'linux_descriptor_limit': 10240}
started = time.monotonic()

def limits():
    import resource
    resource.setrlimit(resource.RLIMIT_AS, (33554432, 33554432))
    resource.setrlimit(resource.RLIMIT_NOFILE, (10240, 10240))

def guard():
    assert read(products / 'closed.json') == build_bytes
    for name, facts in build['sources'].items(): assert identity(read(products / 'source' / name)) == facts
    for role in ('native', 'checked'):
        assert identity(read(products / (role + '-source-file.' + suffix))) == build[role + '_images']['source-file']
    for name, facts in source_proof['source_inputs'].items(): assert identity(read(ROOT / name)) == facts
    assert read(ROOT / 'build/native-input-sharing-20261010/preceding-checkpoint-controls.json') == application_bytes
    for name, facts in application['projected_files'].items(): assert identity(read(ROOT / name)) == facts

def alias_make(alias, target):
    assert alias.parent.resolve().is_relative_to(trees.resolve())
    if windows:
        result = subprocess.run(['cmd.exe', '/d', '/c', 'mklink', '/J', str(alias), str(target)],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)
        assert result.returncode == 0, (result.stdout, result.stderr)
    else: alias.symlink_to(target, target_is_directory=True)

def alias_remove(alias, target):
    assert alias.parent.resolve().is_relative_to(trees.resolve()) and alias.resolve() == target.resolve()
    if windows:
        assert getattr(alias.lstat(), 'st_file_attributes', 0) & 1024
        alias.rmdir()
    else:
        assert alias.is_symlink()
        alias.unlink()

def case_tree(label):
    box = trees / ('case-%04d-%s' % (len(records), label))
    box.mkdir()
    for stem in ('source-a', 'source-b'):
        parent = box / stem / 'nested'
        parent.mkdir(parents=True)
        (parent / 'payload.bin').write_bytes(b'payload-original-0000')
        (parent / 'owned-output.img').write_bytes(b'old-output')
    return box

def regular_oracle(role, label, root, request):
    candidate = Path(request) if Path(request).is_absolute() else Path(root) / request
    requested = hostbuild._disk_absolute(candidate)
    canonical = hostbuild._resolve_disk_regular(requested, 'source-file oracle')
    # A physical directory plus the original no-follow leaf preserves its name.
    # Windows may canonicalize filename case separately in the Python oracle.
    selected = requested.parent.resolve(strict=True) / requested.name
    assert selected.samefile(canonical)
    payload = read(canonical)
    oracle = {'role': role, 'label': label, 'requested_absolute': str(requested),
        'canonical_regular': str(canonical), 'selected_absolute': str(selected), 'payload': identity(payload)}
    oracles.append(oracle)
    physical = str(selected).replace('\\', '/') if windows else str(selected)
    return physical, str(requested)

def call(role, label, mode, root, request, physical='', requested='', expected=0, mutation=None):
    guard()
    root_bytes = root if isinstance(root, bytes) else str(root).encode('utf-8')
    request_bytes = request if isinstance(request, bytes) else str(request).encode('utf-8')
    argv = [str(products / (role + '-source-file.' + suffix)), mode, root_bytes.hex(), request_bytes.hex(),
        physical.encode('utf-8').hex(), requested.encode('utf-8').hex()]
    before = time.monotonic()
    row = {'role': role, 'label': label, 'mode': mode, 'root_hex': root_bytes.hex(), 'request_hex': request_bytes.hex(),
        'expected_physical_hex': physical.encode('utf-8').hex(), 'expected_requested_hex': requested.encode('utf-8').hex(),
        'expected_exit_code': expected, 'timeout_seconds': 60, 'address_space_bytes': None if windows else 33554432,
        'descriptor_limit': None if windows else 10240}
    prefix = b''
    if mutation:
        process = subprocess.Popen(argv, cwd=ROOT, env=environment, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, preexec_fn=None if windows else limits)
        ready, packet = threading.Event(), []
        def read_ready():
            packet.append(process.stdout.read(5)); ready.set()
        threading.Thread(target=read_ready, daemon=True).start()
        if not ready.wait(max(0.001, 60 - (time.monotonic() - before))) or packet != [b'ready']:
            process.kill(); process.communicate(); raise AssertionError(('readiness', row, packet))
        prefix = b'ready'
        row['mutation'] = mutation()
        stdout, stderr = process.communicate(input=b'v', timeout=max(0.001, 60 - (time.monotonic() - before)))
        exit_code = process.returncode
    else:
        result = subprocess.run(argv, cwd=ROOT, env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=60, preexec_fn=None if windows else limits)
        stdout, stderr, exit_code = result.stdout, result.stderr, result.returncode
    stdout = prefix + stdout
    expected_stdout = prefix + (b'accept' if expected == 0 else b'reject')
    row.update(exit_code=exit_code, expected_stdout=expected_stdout.decode('ascii'), stdout=stdout.decode('ascii'),
        stderr=stderr.decode('utf-8', errors='replace'), stdout_identity=identity(stdout), stderr_identity=identity(stderr),
        seconds=time.monotonic() - before, status='pass' if exit_code == expected and stdout == expected_stdout and not stderr else 'fail')
    records.append(row)
    (output / 'running.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    assert row['status'] == 'pass' and row['seconds'] < 60, row
    guard()

try:
    for role in ('native', 'checked'):
        shapes = ('relative', 'absolute', 'parent-alias', 'working-root-alias', 'unicode-file', 'space-file',
            'dots', 'empty-file', 'long-leaf', 'long-parent', 'parent-unicode', 'parent-space', 'same-drive' if windows else 'double-root', *(('leaf-case',) if windows else ()),
            *(() if windows else ('colon-file', 'backslash-file', 'newline-file', 'tab-file',
                'leading-backslash', 'parent-colon', 'parent-backslash', 'parent-newline')))
        for shape in shapes:
            box = case_tree(shape)
            root, request = box / 'source-a/nested', 'payload.bin'
            if shape == 'absolute': request = str(root / request)
            elif shape == 'parent-alias':
                alias_make(box / 'alias', box / 'source-a'); root, request = box, 'alias/nested/payload.bin'
            elif shape == 'working-root-alias':
                alias_make(box / 'alias', root); root = box / 'alias'
            elif shape in ('unicode-file', 'space-file', 'colon-file', 'backslash-file', 'newline-file', 'tab-file', 'leading-backslash'):
                request = {'unicode-file': 'file-\u03b4-\u732b.bin', 'space-file': 'file with spaces.bin', 'colon-file': 'colon:file.bin',
                    'backslash-file': 'back\\file.bin', 'newline-file': 'line\nfile.bin', 'tab-file': 'tab\tfile.bin', 'leading-backslash': '\\file.bin'}[shape]
                (root / request).write_bytes(b'payload-original-0000')
            elif shape.startswith('parent-'):
                root = box / {'parent-colon': 'colon:root', 'parent-backslash': 'back\\', 'parent-newline': 'line\nroot',
                    'parent-unicode': 'parent-\u03b4-\u732b', 'parent-space': 'parent with spaces'}[shape]
                root.mkdir(); (root / request).write_bytes(b'payload-original-0000')
            elif shape == 'dots': request = '../nested/./payload.bin'
            elif shape == 'empty-file': request = 'empty.bin'; (root / request).write_bytes(b'')
            elif shape == 'long-leaf': request = 'x' * 240; (root / request).write_bytes(b'payload-original-0000')
            elif shape == 'long-parent':
                for depth in range(5): root = root / ('part-%d-' % depth + 'x' * 60)
                root.mkdir(parents=True); (root / request).write_bytes(b'payload-original-0000')
            elif shape == 'leaf-case': request = 'PAYLOAD.BIN'
            elif shape == 'same-drive': request = str(root)[:2] + 'payload.bin'
            elif shape == 'double-root': request = '/' + str(root / request)
            physical, absolute = regular_oracle(role, shape, root, request)
            call(role, shape, 'stream', root, request, physical, absolute)
        for mode in ('null-view', 'null-owner-view', 'metadata', 'null-root'):
            box = case_tree(mode); root, request = box / 'source-a/nested', 'payload.bin'
            if mode == 'null-root': request = str(root / request)
            physical, absolute = regular_oracle(role, mode, root, request)
            call(role, mode, mode, root, request, physical, absolute)
        for mode in ('zero-quota', 'max-quota', 'one-quota', 'two-quota', 'null-request', 'null-output', 'null-root'):
            box = case_tree('reject-' + mode)
            call(role, 'reject-' + mode, mode, box / 'source-a/nested', 'payload.bin', expected=2)
        for label, request in (('empty-request', ''), ('missing', 'missing.bin'), ('directory', '.'),
            ('file-parent', 'payload.bin/child'), ('overflow', b'x' * 8192), ('bad-continuation', b'bad-\xc2'),
            ('surrogate', b'bad-\xed\xa0\x80'), ('above-scalar', b'bad-\xf4\x90\x80\x80'), ('overlong', b'bad-\xc0\xaf')):
            box = case_tree(label)
            if isinstance(request, str):
                try: hostbuild._resolve_disk_regular(box / 'source-a/nested' / request, 'negative source-file oracle')
                except hostbuild.DiskImageError: pass
                else: raise AssertionError(('negative oracle accepted', label))
            call(role, label, 'stream', box / 'source-a/nested', request, expected=2)
        for kind in ('linked-directory-leaf', *(() if windows else ('linked-file-leaf', 'broken-leaf', 'fifo'))):
            box = case_tree(kind); root = box / 'source-a/nested'; leaf = root / 'selected.bin'
            if kind == 'linked-directory-leaf': alias_make(leaf, box / 'source-b/nested')
            elif kind == 'linked-file-leaf': leaf.symlink_to(root / 'payload.bin')
            elif kind == 'broken-leaf': leaf.symlink_to(root / 'missing.bin')
            else: os.mkfifo(leaf)
            try: hostbuild._resolve_disk_regular(leaf, 'negative leaf oracle')
            except hostbuild.DiskImageError: pass
            else: raise AssertionError(('negative leaf oracle accepted', kind))
            call(role, kind, 'stream', root, leaf.name, expected=2)
        for mode in ('metadata', 'stream', 'borrow'):
            for kind in ('unchanged', 'retarget', 'remove', 'broken', 'directory-replacement', 'payload-restored-time'):
                box = case_tree(mode + '-' + kind); alias = box / 'alias'; target = box / 'source-\u03b4-\u732b'
                (box / 'source-a').rename(target)
                alias_make(alias, target); root, request = alias / 'nested', 'payload.bin'
                physical, absolute = regular_oracle(role, mode + '-' + kind, root, request)
                original_file = Path(physical)
                def mutate():
                    if kind == 'unchanged': return {'kind': kind}
                    if kind == 'payload-restored-time':
                        info = original_file.stat(); original_file.write_bytes(b'Payload-original-0000')
                        os.utime(original_file, ns=(info.st_atime_ns, info.st_mtime_ns))
                        return {'kind': kind, 'size_preserved': original_file.stat().st_size == info.st_size,
                            'mtime_preserved': original_file.stat().st_mtime_ns == info.st_mtime_ns}
                    alias_remove(alias, target)
                    if kind == 'retarget': alias_make(alias, box / 'source-b')
                    elif kind == 'broken': alias_make(alias, box / 'missing')
                    elif kind == 'directory-replacement':
                        (alias / 'nested').mkdir(parents=True); (alias / 'nested/payload.bin').write_bytes(b'payload-original-0000')
                    return {'kind': kind, 'original_physical_payload': identity(read(original_file))}
                expected = 0 if kind == 'unchanged' or (kind == 'payload-restored-time' and mode == 'metadata') else 2
                call(role, mode + '-' + kind, 'hold-' + mode, root, request, physical, absolute, expected, mutate)
    guard()
    receipt.update(status='pass', calls=len(records), positive_calls=sum(row['expected_exit_code'] == 0 for row in records),
        negative_calls=sum(row['expected_exit_code'] != 0 for row in records), normal_source_inputs_unchanged=99,
        installed_root_seed_files_unchanged=15)
except BaseException as error:
    receipt.update(status='fail', error=repr(error)); traceback.print_exc()
receipt['seconds'] = time.monotonic() - started
(output / 'closed.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({k: receipt.get(k) for k in ('status', 'calls', 'positive_calls', 'negative_calls', 'seconds', 'error')}))
raise SystemExit(0 if receipt['status'] == 'pass' else 1)

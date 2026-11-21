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
output = BASE / ('common-retained-optional-contracts-' + host + '1')
assert not output.exists()
output.mkdir()
trees = output / 'trees' if windows else Path(tempfile.mkdtemp(prefix='cupid-source-optional-', dir='/var/tmp'))
trees.mkdir(exist_ok=True)
sentinels = {name: 'forbidden-host-' + name for name in ('CC', 'CXX', 'CPP', 'AS', 'AR', 'LD', 'NM', 'OBJCOPY', 'NASM')}
environment = {**os.environ, **sentinels}
source_proof = json.loads(read(BASE / 'source-controls.json'))
application_bytes = read(ROOT / 'build/native-input-sharing-20261010/preceding-checkpoint-controls.json')
application = json.loads(application_bytes)
records, oracles = [], []
receipt = {'schema': 'cupid.native-source-optional-contracts.v1', 'status': 'running', 'host': host,
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
        assert identity(read(products / (role + '-source-optional.' + suffix))) == build[role + '_images']['source-optional']
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
    for stem in ('source-a-\u03b4-\u732b', 'source-b-\u03b4-\u732b'):
        for parent in (box / stem, box / stem / 'nested'):
            parent.mkdir()
            (parent / 'payload.bin').write_bytes(b'payload-original-0000')
            (parent / 'anchor.bin').write_bytes(b'anchor-original-00000')
            (parent / 'owned-output.img').write_bytes(b'old-output')
    return box


def unc(path, server='localhost'):
    text = str(path)
    assert windows and len(text) > 3 and text[1:3] == ':\\'
    return '\\\\' + server + '\\' + text[0].upper() + '$\\' + text[3:]


def optional_oracle(role, label, root, request):
    candidate = Path(request) if Path(request).is_absolute() else Path(root) / request
    requested = hostbuild._disk_absolute(candidate)
    assert not hostbuild._disk_is_link_or_junction(requested, 'optional-source oracle')
    canonical = hostbuild._resolve_disk_regular(requested, 'optional-source oracle') if requested.exists() else None
    parent = requested.parent
    while not parent.exists():
        assert parent != parent.parent
        parent = parent.parent
    physical_parent = parent.resolve(strict=True)
    assert physical_parent.is_dir()
    suffix = requested.relative_to(parent).as_posix()
    selected = physical_parent / requested.relative_to(parent)
    if canonical is not None: assert selected.samefile(canonical)
    kind = 'file' if canonical is not None else 'none'
    normalized = lambda path: str(path).replace('\\', '/') if windows else str(path)
    oracles.append({'role': role, 'label': label, 'requested_absolute': str(requested),
        'original_nearest_parent': str(parent), 'physical_parent': str(physical_parent),
        'logical_suffix': suffix, 'selected_absolute': str(selected), 'kind': kind,
        'canonical_regular': str(canonical) if canonical is not None else None,
        'payload': identity(read(canonical)) if canonical is not None else None})
    parent_name = normalized(physical_parent)
    if windows and physical_parent.drive.startswith('\\\\') and physical_parent == Path(physical_parent.anchor):
        parent_name = parent_name.rstrip('/')
    return {'physical': normalized(selected), 'requested': str(requested),
        'parent': parent_name, 'logical': suffix, 'kind': kind}


def call(role, label, mode, root, request, physical='', requested='', parent='', logical='', kind='none', expected=0, mutation=None):
    guard()
    root_bytes = root if isinstance(root, bytes) else str(root).encode('utf-8')
    request_bytes = request if isinstance(request, bytes) else str(request).encode('utf-8')
    argv = [str(products / (role + '-source-optional.' + suffix)), mode, root_bytes.hex(), request_bytes.hex(),
        physical.encode('utf-8').hex(), requested.encode('utf-8').hex(), parent.encode('utf-8').hex(), logical.encode('utf-8').hex(), kind]
    before = time.monotonic()
    row = {'role': role, 'label': label, 'mode': mode, 'root_hex': root_bytes.hex(), 'request_hex': request_bytes.hex(),
        'expected_physical_hex': physical.encode('utf-8').hex(), 'expected_requested_hex': requested.encode('utf-8').hex(),
        'expected_parent_hex': parent.encode('utf-8').hex(), 'expected_logical_hex': logical.encode('utf-8').hex(), 'expected_kind': kind,
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
        shapes = ('present', 'missing-leaf', 'missing-parent', 'missing-deep', 'absolute',
            'parent-alias', 'working-root-alias', 'unicode-leaf', 'space-leaf', 'long-leaf',
            'long-parent', 'existing-parent-chain', 'dots', 'empty-file',
            'same-drive' if windows else 'double-root', 'volume-root',
            *(('leaf-case', 'unc-present', 'unc-missing', 'unc-parent', 'unc-address', 'unc-slashes', 'unc-share-root') if windows
                else ('colon-leaf', 'backslash-leaf', 'newline-leaf', 'tab-leaf', 'leading-backslash',
                    'parent-colon', 'parent-backslash', 'parent-newline')))
        for shape in shapes:
            box = case_tree(shape)
            root = box / 'source-a-\u03b4-\u732b/nested'
            request = 'missing.bin'
            if shape in ('present', 'unc-present'): request = 'payload.bin'
            elif shape == 'missing-parent': request = 'ghost/missing.bin'
            elif shape in ('missing-deep', 'absolute'): request = 'ghost/second/third/missing.bin'
            elif shape == 'parent-alias':
                alias_make(box / 'alias', box / 'source-a-\u03b4-\u732b')
                root, request = box, 'alias/nested/ghost/deep/missing.bin'
            elif shape == 'working-root-alias':
                alias_make(box / 'alias', root); root = box / 'alias'; request = 'ghost/deep/missing.bin'
            elif shape in ('unicode-leaf', 'space-leaf', 'colon-leaf', 'backslash-leaf', 'newline-leaf', 'tab-leaf', 'leading-backslash'):
                request = {'unicode-leaf': 'missing-\u03b4-\u732b.bin', 'space-leaf': 'missing with spaces.bin',
                    'colon-leaf': 'colon:missing.bin', 'backslash-leaf': 'back\\missing.bin',
                    'newline-leaf': 'line\nmissing.bin', 'tab-leaf': 'tab\tmissing.bin',
                    'leading-backslash': '\\missing.bin'}[shape]
            elif shape == 'long-leaf': request = 'x' * 240
            elif shape == 'long-parent':
                for depth in range(5): root = root / ('part-%d-' % depth + 'x' * 60)
                root.mkdir(parents=True); request = 'ghost/deep/missing.bin'
            elif shape == 'existing-parent-chain':
                (root / 'existing/another').mkdir(parents=True); request = 'existing/another/ghost/deep/missing.bin'
            elif shape == 'dots': request = '../nested/./ghost/../missing.bin'
            elif shape == 'empty-file': request = 'empty.bin'; (root / request).write_bytes(b'')
            elif shape == 'leaf-case': request = 'PAYLOAD.BIN'
            elif shape.startswith('parent-'):
                root = box / {'parent-colon': 'colon:root', 'parent-backslash': 'back\\', 'parent-newline': 'line\nroot'}[shape]
                root.mkdir(); request = 'ghost/deep/missing.bin'
            elif shape == 'same-drive': request = str(root)[:2] + 'ghost/missing.bin'
            elif shape == 'double-root': request = '/' + str(root / 'ghost/deep/missing.bin')
            elif shape == 'volume-root':
                root = Path(root.anchor)
                request = 'cupid-source-optional-absent-20261010-%s-%s/ghost/missing.bin' % (role, box.name)
                assert not (root / request.split('/')[0]).exists()
            if shape == 'absolute': request = str(root / request)
            if shape.startswith('unc-'):
                if shape == 'unc-share-root':
                    root = Path('\\\\localhost\\' + str(root)[0].upper() + '$\\')
                    request = 'cupid-source-optional-absent-20261010-%s-%s/ghost/missing.bin' % (role, box.name)
                    assert not (root / request.split('/')[0]).exists()
                else:
                    root = Path(unc(root, '127.0.0.1' if shape == 'unc-address' else 'localhost'))
                    if shape in ('unc-parent', 'unc-address', 'unc-slashes'): request = 'ghost/deep/missing.bin'
                    if shape == 'unc-slashes': root = str(root).replace('\\', '/')
            call(role, shape, 'optional', root, request, **optional_oracle(role, shape, root, request))
        for present in (False, True):
            for mode in ('null-view', 'null-owner-view', 'null-close', 'null-root'):
                box = case_tree(mode); root = box / 'source-a-\u03b4-\u732b/nested'
                request = 'payload.bin' if present else 'ghost/deep/missing.bin'
                if mode == 'null-root': request = str(root / request)
                call(role, mode + ('-present' if present else '-absent'), mode, root, request,
                    **optional_oracle(role, mode, root, request))
        box = case_tree('required-present'); root = box / 'source-a-\u03b4-\u732b/nested'
        call(role, 'required-present', 'required', root, 'payload.bin', **optional_oracle(role, 'required-present', root, 'payload.bin'))
        call(role, 'required-missing', 'required', root, 'ghost/deep/missing.bin', expected=2)
        call(role, 'absent-stream-poison', 'missing-stream', root, 'ghost/deep/missing.bin', expected=2,
            **optional_oracle(role, 'absent-stream-poison', root, 'ghost/deep/missing.bin'))
        for mode in ('zero-quota', 'max-quota', 'one-quota', 'two-quota', 'null-request', 'null-output', 'null-kind', 'null-root'):
            box = case_tree('reject-' + mode); root = box / 'source-a-\u03b4-\u732b/nested'
            call(role, 'reject-' + mode, mode, root, 'ghost/deep/missing.bin', expected=2)
        invalid = [('empty-request', ''), ('directory', '.'), ('file-parent', 'payload.bin/child'),
            ('overflow', b'x' * 8192), ('bad-continuation', b'bad-\xc2'), ('overlong-utf8', b'bad-\xc0\x80'),
            ('surrogate', b'bad-\xed\xa0\x80'), ('above-scalar', b'bad-\xf4\x90\x80\x80'),
            ('bad-missing-suffix', b'ghost/deep-\xc2'), ('overlong-missing-component', 'ghost/' + 'x' * 1024)]
        if windows: invalid.append(('reserved-missing-component', 'ghost/colon:name'))
        for label, request in invalid:
            box = case_tree(label); root = box / 'source-a-\u03b4-\u732b/nested'
            call(role, label, 'optional', root, request, expected=2)
        for special in (('junction',) if windows else ('directory-link', 'file-link', 'broken-link', 'fifo')):
            box = case_tree(special); root = box / 'source-a-\u03b4-\u732b/nested'; leaf = root / 'special'
            if special == 'junction': alias_make(leaf, root)
            elif special == 'directory-link': leaf.symlink_to(root, target_is_directory=True)
            elif special == 'file-link': leaf.symlink_to(root / 'payload.bin')
            elif special == 'broken-link': leaf.symlink_to(root / 'unavailable')
            else: os.mkfifo(leaf)
            call(role, special, 'optional', root, 'special', expected=2)
        for mode in ('hold-view', 'hold-borrow'):
            for mutation_kind in ('unchanged', 'sibling', 'appear-file', 'appear-directory', 'appear-parent',
                    'retarget', 'remove', 'broken', 'plain'):
                box = case_tree(mode + '-' + mutation_kind)
                physical_root = box / 'source-a-\u03b4-\u732b/nested'
                other_root = box / 'source-b-\u03b4-\u732b/nested'
                alias = box / 'alias'; alias_make(alias, physical_root)
                request = 'missing.bin' if mutation_kind in ('appear-file', 'appear-directory') else 'ghost/deep/missing.bin'
                anchor_before = identity(read(physical_root / 'anchor.bin'))
                payload_before = identity(read(physical_root / 'payload.bin'))
                def mutate():
                    if mutation_kind == 'sibling': (physical_root / 'unrelated.bin').write_bytes(b'sibling')
                    elif mutation_kind == 'appear-file': (physical_root / 'missing.bin').write_bytes(b'appeared')
                    elif mutation_kind == 'appear-directory': (physical_root / 'missing.bin').mkdir()
                    elif mutation_kind == 'appear-parent': (physical_root / 'ghost').mkdir()
                    elif mutation_kind in ('retarget', 'remove', 'broken', 'plain'):
                        alias_remove(alias, physical_root)
                        if mutation_kind == 'retarget': alias_make(alias, other_root)
                        elif mutation_kind == 'broken': alias_make(alias, box / 'unavailable')
                        elif mutation_kind == 'plain':
                            alias.mkdir(); (alias / 'payload.bin').write_bytes(b'payload-original-0000')
                            (alias / 'anchor.bin').write_bytes(b'anchor-original-00000')
                    assert identity(read(physical_root / 'anchor.bin')) == anchor_before
                    assert identity(read(physical_root / 'payload.bin')) == payload_before
                    return {'kind': mutation_kind, 'original_anchor_unchanged': True, 'original_payload_unchanged': True,
                        'final_request_exists': (alias / request).exists()}
                call(role, 'absent-' + mode + '-' + mutation_kind, mode, alias, request,
                    expected=0 if mutation_kind in ('unchanged', 'sibling') else 2, mutation=mutate,
                    **optional_oracle(role, mutation_kind, alias, request))
        for mode in ('hold-members', 'hold-required'):
            for mutation_kind in ('unchanged', 'sibling'):
                box = case_tree(mode + '-' + mutation_kind)
                root = box / 'source-a-\u03b4-\u732b/nested'
                request = 'payload.bin' if mode == 'hold-required' else 'ghost/deep/missing.bin'
                def mutate_snapshot():
                    if mutation_kind == 'sibling': (root / 'unrelated.bin').write_bytes(b'sibling')
                    return {'kind': mutation_kind, 'exact_membership_captured': mode == 'hold-members',
                        'required_root_metadata_contract': mode == 'hold-required'}
                call(role, mode + '-' + mutation_kind, mode, root, request,
                    expected=0 if mutation_kind == 'unchanged' else 2, mutation=mutate_snapshot,
                    **optional_oracle(role, mutation_kind, root, request))
        for mode in ('hold-view', 'hold-stream', 'hold-borrow'):
            for mutation_kind in ('unchanged', 'retarget', 'remove', 'broken', 'plain', 'restored-time'):
                box = case_tree(mode + '-' + mutation_kind)
                physical_root = box / 'source-a-\u03b4-\u732b/nested'
                other_root = box / 'source-b-\u03b4-\u732b/nested'
                alias = box / 'alias'; alias_make(alias, physical_root)
                before = read(physical_root / 'payload.bin'); facts = (physical_root / 'payload.bin').stat()
                def mutate_present():
                    if mutation_kind in ('retarget', 'remove', 'broken', 'plain'):
                        alias_remove(alias, physical_root)
                        if mutation_kind == 'retarget': alias_make(alias, other_root)
                        elif mutation_kind == 'broken': alias_make(alias, box / 'unavailable')
                        elif mutation_kind == 'plain':
                            alias.mkdir(); (alias / 'payload.bin').write_bytes(before)
                            (alias / 'anchor.bin').write_bytes(b'anchor-original-00000')
                    elif mutation_kind == 'restored-time':
                        (physical_root / 'payload.bin').write_bytes(b'payload-modified-0000')
                        os.utime(physical_root / 'payload.bin', ns=(facts.st_atime_ns, facts.st_mtime_ns))
                    after = read(physical_root / 'payload.bin')
                    if mutation_kind != 'restored-time': assert after == before
                    else:
                        assert len(after) == len(before) and after != before
                        assert (physical_root / 'payload.bin').stat().st_mtime_ns == facts.st_mtime_ns
                    return {'kind': mutation_kind, 'original_payload': identity(before), 'current_payload': identity(after),
                        'original_payload_unchanged': after == before, 'timestamp_restored': mutation_kind == 'restored-time'}
                call(role, 'present-' + mode + '-' + mutation_kind, mode, alias, 'payload.bin',
                    expected=0 if mutation_kind == 'unchanged' or (mode == 'hold-view' and mutation_kind == 'restored-time') else 2,
                    mutation=mutate_present, **optional_oracle(role, mutation_kind, alias, 'payload.bin'))
    guard()
    receipt.update(status='pass', calls=len(records), positive_calls=sum(row['expected_exit_code'] == 0 for row in records),
        negative_calls=sum(row['expected_exit_code'] != 0 for row in records),
        actual_python_file_and_missing_oracles=len(oracles), original_required_caller_source_unchanged=True,
        normal_source_inputs_unchanged=99, guarded_current_root_files=20)
except BaseException as error:
    receipt.update(status='fail', error=repr(error))
    traceback.print_exc()
receipt['seconds'] = time.monotonic() - started
(output / 'closed.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({key: receipt.get(key) for key in ('status', 'host', 'calls', 'positive_calls', 'negative_calls', 'seconds', 'error')}))
raise SystemExit(0 if receipt['status'] == 'pass' else 1)


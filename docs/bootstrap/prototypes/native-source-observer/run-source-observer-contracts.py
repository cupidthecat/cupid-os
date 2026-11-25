from pathlib import Path
import concurrent.futures
import hashlib
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
sys.path.insert(0, str(ROOT))
from tools import hostbuild

host, suffix = ('windows', 'exe') if os.name == 'nt' else ('linux', 'elf')
products = BASE / ('products-' + host + '1')
output = BASE / ('source-contracts-' + host + '1')
assert not output.exists()
output.mkdir()
case = output / 'cases' if os.name == 'nt' else Path(tempfile.mkdtemp(prefix='cupid-source-observer-', dir='/var/tmp'))
if os.name == 'nt': case.mkdir()
build_bytes = read(products / 'closed.json')
build = json.loads(build_bytes)
assert build['status'] == 'pass'
proof = json.loads(read(BASE / 'source-controls.json'))
programs = {role: products / (role + '-source-names.' + suffix) for role in ('native', 'checked')}
sentinels = {name: 'forbidden-host-' + name for name in ('CC', 'CXX', 'CPP', 'AS', 'AR', 'LD', 'NM', 'OBJCOPY', 'NASM')}
environment = {**os.environ, **sentinels}
records, oracles = [], []
started = time.monotonic()
receipt = {'schema': 'cupid.native-source-observer-contracts.v1', 'status': 'running',
    'host': host, 'build': identity(build_bytes), 'runner': identity(read(Path(__file__))),
    'case_root': str(case), 'records': records, 'actual_python_oracles': oracles,
    'producer_sentinels': sentinels, 'timeout_seconds': 60,
    'address_space_bytes': None if os.name == 'nt' else 32 * 1024 * 1024,
    'descriptor_limit': None if os.name == 'nt' else 10240}


def limits():
    import resource
    resource.setrlimit(resource.RLIMIT_AS, (32 * 1024 * 1024, 32 * 1024 * 1024))
    resource.setrlimit(resource.RLIMIT_NOFILE, (10240, 10240))


def guard(committed=False):
    for role, path in programs.items(): assert identity(read(path)) == build[role + '_images']['source-names']
    for name, facts in build['sources'].items(): assert identity(read(products / 'source' / name)) == facts
    for name, facts in proof['source_inputs'].items():
        data = read(ROOT / name)
        assert identity(data) == facts
        if committed: assert data == git_bytes(proof['source_revision'], name)
    for name in SEED_FILES: assert read(ROOT / name) == git_bytes('HEAD', name)


def hex_arg(value):
    return (value if isinstance(value, bytes) else str(value).encode('utf-8')).hex()


def invoke(role, profile, mode, root, logical, expected_code, expected_stdout='', names=(), mutation=None):
    before = time.monotonic()
    argv = [str(programs[role]), profile, mode, hex_arg(root), hex_arg(logical), *[hex_arg(name) for name in names]]
    row = {'role': role, 'profile': profile, 'mode': mode, 'root_hex': hex_arg(root), 'logical_hex': hex_arg(logical),
        'names_hex': [hex_arg(name) for name in names], 'expected_exit_code': expected_code,
        'expected_stdout': expected_stdout, 'timeout_seconds': 60, 'address_space_bytes': receipt['address_space_bytes'],
        'descriptor_limit': receipt['descriptor_limit']}
    if mutation is None:
        result = subprocess.run(argv, cwd=case, env=environment, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=60, preexec_fn=None if os.name == 'nt' else limits)
        code, stdout, stderr = result.returncode, result.stdout, result.stderr
    else:
        deadline = time.monotonic() + 60
        process = subprocess.Popen(argv, cwd=case, env=environment, stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, preexec_fn=None if os.name == 'nt' else limits)
        try:
            with concurrent.futures.ThreadPoolExecutor(max_workers=1) as reader:
                future = reader.submit(process.stdout.read, 5)
                try:
                    prefix = future.result(timeout=max(0.001, deadline - time.monotonic()))
                except BaseException:
                    process.kill()
                    raise
            assert prefix == b'ready', (role, profile, mode, prefix, process.poll())
            row['mutation'] = mutation()
            tail, stderr = process.communicate(input=b'x', timeout=max(0.001, deadline - time.monotonic()))
            stdout, code = prefix + tail, process.returncode
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
    row.update(exit_code=code, stdout=stdout.decode('ascii'), stderr=stderr.decode('utf-8'), seconds=time.monotonic() - before)
    row['status'] = 'pass' if code == expected_code and stdout == expected_stdout.encode('ascii') and not stderr else 'fail'
    records.append(row)
    (output / 'running.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    assert row['status'] == 'pass', row
    return row


try:
    guard(True)
    ordinary = case / 'ordinary'
    ordinary.mkdir()
    files = [('payload.bin', bytes(range(256))), ('empty.bin', b''), ('\u03b4-\u732b.bin', b'unicode\n'),
        ('name space.bin', b'space'), ('dots..name.bin', b'dots'), ('-dash.bin', b'dash')]
    if os.name != 'nt':
        files.extend((('colon:name.bin', b'colon'), ('backslash\\name.bin', b'backslash'),
            ('line\nname.bin', b'newline'), ('tab\tname.bin', b'tab'), ('\\leading.bin', b'leading'),
            ('mix:back\\slash.bin', b'mixed')))
    for name, data in files:
        path = ordinary / name
        path.write_bytes(data)
        requested = hostbuild._disk_absolute(path)
        resolved = hostbuild._resolve_disk_regular(path, 'source profile oracle')
        assert read(resolved) == data
        oracles.append({'requested': str(requested), 'resolved': str(resolved), 'payload': identity(data)})
    members = case / 'members'
    members.mkdir()
    member_names = ['plain', '\u732b'] + (['colon:name', 'backslash\\name'] if os.name != 'nt' else [])
    for name in member_names: (members / name).write_bytes(b'member')
    nested_name = 'nested:directory\\name' if os.name != 'nt' else 'nested-directory'
    nested = case / nested_name
    nested.mkdir()
    (nested / 'payload.bin').write_bytes(b'nested')
    source_root = case / ('source:root\\\u03b4' if os.name != 'nt' else 'source-\u03b4')
    source_root.mkdir()
    (source_root / 'payload.bin').write_bytes(b'source root')
    if os.name != 'nt':
        (case / 'linked.bin').symlink_to(ordinary / 'payload.bin')
        (case / 'broken.bin').symlink_to(case / 'absent-target')
        (case / 'linked-parent').symlink_to(ordinary, target_is_directory=True)
        os.mkfifo(case / 'fifo')
    for role in programs:
        for profile in ('strict', 'source'):
            invoke(role, profile, 'open', case, '', 0)
            invoke(role, profile, 'root-kind', case, '', 0, 'directory')
            for name, data in files:
                allowed = not (os.name != 'nt' and profile == 'strict' and (':' in name or '\\' in name))
                logical = 'ordinary/' + name
                invoke(role, profile, 'kind', case, logical, 0 if allowed else 2, 'file' if allowed else '')
                invoke(role, profile, 'file', case, logical, 0 if allowed else 2, str(len(data)) if allowed else '')
                invoke(role, profile, 'stream', case, logical, 0 if allowed else 2,
                    str(len(data)) + ':' + hashlib.sha256(data).hexdigest() if allowed else '')
                invoke(role, profile, 'same', case, logical, 0 if allowed else 2)
            allowed = os.name == 'nt' or profile == 'source'
            invoke(role, profile, 'open', source_root, '', 0 if allowed else 2)
            invoke(role, profile, 'kind', source_root, 'payload.bin', 0 if allowed else 2, 'file' if allowed else '')
            invoke(role, profile, 'kind', case, nested_name + '/payload.bin', 0 if allowed else 2, 'file' if allowed else '')
            invoke(role, profile, 'membership', case, 'members', 0 if allowed else 2, names=member_names)
            invoke(role, profile, 'kind', case, 'missing/child.bin', 0, 'missing')
            special = 'absent:directory\\name/child.bin' if os.name != 'nt' else 'absent-directory/child.bin'
            invoke(role, profile, 'kind', case, special, 0 if allowed else 2, 'missing' if allowed else '')
            for mode in ('zero-quota', 'max-quota', 'null-root', 'null-owner', 'null-kind', 'null-logical', 'null-stream', 'null-size'):
                invoke(role, profile, mode, case, 'ordinary/payload.bin', 2)
            for logical in ('', '.', '..', '/ordinary/payload.bin', 'ordinary//payload.bin', 'ordinary/../payload.bin',
                'ordinary/payload.bin/', 'missing/../suffix', 'missing/' + 'x' * 1024, b'missing/bad\xc0\xaf',
                b'missing/bad\xed\xa0\x80'):
                invoke(role, profile, 'kind', case, logical, 2)
            invoke(role, profile, 'membership', case, 'members', 2, names=('plain', 'plain'))
            invoke(role, profile, 'membership', case, 'members', 2, names=('.',))
            invoke(role, profile, 'membership', case, 'members', 2, names=(b'bad\xc0\xaf',))
            if os.name != 'nt':
                for logical in ('linked.bin', 'broken.bin', 'linked-parent/payload.bin', 'fifo'):
                    invoke(role, profile, 'kind', case, logical, 2)
        for kind in ('stream-edit', 'metadata-restored-edit', 'missing-appearance', 'membership-add', 'parent-replace'):
            held = case / (role + '-' + kind)
            held.mkdir()
            leaf_name = 'file:with\\name' if os.name != 'nt' else 'payload.bin'
            leaf = held / leaf_name
            leaf.write_bytes(b'original payload')
            parent_name = 'parent:with\\name' if os.name != 'nt' else 'parent'
            parent = held / parent_name
            parent.mkdir()
            (parent / 'payload.bin').write_bytes(b'parent payload')
            before_root = held.stat()
            before_leaf = leaf.stat()
            expected = 0 if kind == 'metadata-restored-edit' or (kind == 'parent-replace' and os.name == 'nt') else 2
            if kind in ('stream-edit', 'metadata-restored-edit'):
                logical = leaf_name
                mode = 'hold-stream' if kind == 'stream-edit' else 'hold-kind'
                def mutate(leaf=leaf, before=before_leaf):
                    leaf.write_bytes(b'changed! payload')
                    os.utime(leaf, ns=(before.st_atime_ns, before.st_mtime_ns))
                    assert leaf.stat().st_size == before.st_size
                    return {'same_size_restored_mtime': True, 'after': identity(read(leaf))}
                names = ()
            elif kind == 'missing-appearance':
                logical = 'missing:leaf' if os.name != 'nt' else 'missing-leaf'
                mode, names = 'hold-kind', ()
                def mutate(held=held, logical=logical, before=before_root):
                    (held / logical).write_bytes(b'appeared')
                    os.utime(held, ns=(before.st_atime_ns, before.st_mtime_ns))
                    return {'first_absence_appeared_root_mtime_restored': True}
            elif kind == 'membership-add':
                logical, mode, names = parent_name, 'hold-membership', ('payload.bin',)
                def mutate(parent=parent):
                    (parent / 'extra').write_bytes(b'new member')
                    return {'membership_added': True}
            else:
                logical, mode, names = parent_name + '/payload.bin', 'hold-kind', ()
                replacement = held / 'displaced-parent'
                assert parent.resolve().is_relative_to(held.resolve()) and replacement.resolve().is_relative_to(held.resolve())
                def mutate(parent=parent, replacement=replacement, held=held, before=before_root):
                    try:
                        parent.rename(replacement)
                    except OSError as error:
                        assert os.name == 'nt' and error.winerror in (5, 32), error
                        return {'retained_parent_rename_denied': True, 'winerror': error.winerror}
                    assert os.name != 'nt', 'Windows retained parent unexpectedly allowed replacement'
                    parent.mkdir()
                    (parent / 'payload.bin').write_bytes(b'parent payload')
                    os.utime(held, ns=(before.st_atime_ns, before.st_mtime_ns))
                    return {'parent_binding_replaced_root_mtime_restored': True}
            row = invoke(role, 'source', mode, held, logical, expected, 'ready', names=names, mutation=mutate)
            if kind == 'parent-replace' and os.name == 'nt':
                parent.rename(replacement)
                assert replacement.is_dir() and not parent.exists()
                row['retained_parent_rename_after_close'] = True
    guard(True)
    receipt.update(status='pass', calls=len(records), positive_calls=sum(row['exit_code'] == 0 for row in records),
        negative_calls=sum(row['exit_code'] == 2 for row in records), actual_oracle_cases=len(oracles),
        all_original_bounds_preserved=True, normal_99_committed_inputs_unchanged=True, installed_root_seed_files_unchanged=15)
except BaseException as error:
    receipt.update(status='fail', error=repr(error))
    traceback.print_exc()
receipt['seconds'] = time.monotonic() - started
(output / 'closed.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({key: receipt.get(key) for key in ('status', 'host', 'calls', 'positive_calls', 'negative_calls', 'seconds', 'error')}))
raise SystemExit(0 if receipt['status'] == 'pass' else 1)

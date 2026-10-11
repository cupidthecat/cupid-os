from pathlib import Path
import ast
import hashlib
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
windows = os.name == 'nt'
host, suffix, index = ('windows', 'exe', '1') if windows else ('linux', 'elf', '1')
products = BASE / ('stage-products-' + host + index)
build_bytes = (products / 'closed.json').read_bytes()
build = json.loads(build_bytes)
assert build['status'] == 'pass'
output = BASE / ('stage-contracts-' + host + '1')
assert not output.exists()
output.mkdir()
trees = output / 'trees' if windows else Path(tempfile.mkdtemp(prefix='cupid-source-stage-', dir='/var/tmp'))
trees.mkdir(exist_ok=True)
sys.path.insert(0, str(ROOT))
from tools import hostbuild
from replay_guard import make_guard
require_captured_inputs = make_guard(ROOT, BASE)
identity = lambda data: {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
hostbuild_bytes = (ROOT / 'tools/hostbuild.py').read_bytes()
tree_ast = ast.parse(hostbuild_bytes.decode('utf-8'))
function = next(node for node in tree_ast.body if isinstance(node, ast.FunctionDef) and node.name == 'create_or_update_image')
selection_loop = next(node for node in ast.walk(function) if isinstance(node, ast.For) and isinstance(node.iter, ast.Name) and node.iter.id == 'stage_files')
selection = compile(ast.Module([selection_loop], []), 'tools/hostbuild.py:stage-selection', 'exec')
missing_loop = next(node for node in ast.walk(function) if isinstance(node, ast.For) and isinstance(node.iter, ast.Name) and node.iter.id == 'missing_stages' and any(isinstance(child, ast.If) for child in node.body))
condition = next(node.test for node in missing_loop.body if isinstance(node, ast.If))
assert ast.unparse(condition) == "stage.source.exists() or _disk_is_link_or_junction(stage.source, 'missing stage input')"
predicate = compile(ast.Expression(condition), 'tools/hostbuild.py:missing-stage-publication', 'eval')
sentinels = {name: 'forbidden-host-' + name for name in ('CC', 'CXX', 'CPP', 'AS', 'AR', 'LD', 'NM', 'OBJCOPY', 'NASM')}
records, oracles, delete_controls = [], [], []
started = time.monotonic()
receipt = {'schema': 'cupid.native-source-stage-contracts.v1', 'status': 'running', 'host': host,
    'build': identity(build_bytes), 'hostbuild': identity(hostbuild_bytes), 'records': records, 'oracles': oracles,
    'runner': identity(Path(__file__).read_bytes()), 'producer_sentinels': sentinels, 'windows_delete_controls': delete_controls,
    'timeout_seconds': 60, 'linux_address_space_bytes': 33554432, 'linux_descriptor_limit': 10240}

def guard(full=False):
    require_captured_inputs()
    assert (products / 'closed.json').read_bytes() == build_bytes
    assert (ROOT / 'tools/hostbuild.py').read_bytes() == hostbuild_bytes
    for name, facts in build['sources'].items(): assert identity((products / 'source' / name).read_bytes()) == facts
    for name, facts in build['objects'].items(): assert identity((products / 'objects' / name).read_bytes()) == facts
    for role in ('native', 'checked'):
        assert identity((products / (role + '-source-stage.' + suffix)).read_bytes()) == build[role + '_images']['source-stage']
    if full:
        for name, facts in build['normal_source_inputs'].items(): assert identity((ROOT / name).read_bytes()) == facts

def limits():
    import resource
    resource.setrlimit(resource.RLIMIT_AS, (33554432, 33554432))
    resource.setrlimit(resource.RLIMIT_NOFILE, (10240, 10240))

def alias_make(alias, target):
    assert alias.parent.resolve().is_relative_to(trees.resolve())
    if windows:
        result = subprocess.run(['cmd.exe', '/d', '/c', 'mklink', '/J', str(alias), str(target)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=10)
        assert result.returncode == 0, (result.stdout, result.stderr)
    else: alias.symlink_to(target, target_is_directory=True)

def box(label):
    p = trees / (str(len(records)) + '-' + label)
    p.mkdir()
    (p / 'payload.bin').write_bytes(b'original')
    return p

def oracle(label, requested, final=False):
    env = {'stage_files': [hostbuild.StageFile(requested, '/payload.bin')], 'present_stages': [], 'missing_stages': [],
           '_disk_absolute': hostbuild._disk_absolute, '_disk_is_link_or_junction': hostbuild._disk_is_link_or_junction,
           '_resolve_disk_regular': hostbuild._resolve_disk_regular, 'DiskImageError': hostbuild.DiskImageError, 'StageFile': hostbuild.StageFile}
    try:
        if final:
            env['stage'] = hostbuild.StageFile(requested, '/payload.bin')
            result = 'reject' if eval(predicate, env) else 'none'
        else:
            exec(selection, env)
            assert len(env['present_stages']) + len(env['missing_stages']) == 1
            result = 'file' if env['present_stages'] else 'none'
        error = None
    except hostbuild.DiskImageError as e:
        result, error = 'reject', str(e)
    except OSError as e:
        result, error = 'reject', repr(e)
    oracles.append({'label': label, 'phase': 'publication' if final else 'selection', 'request': str(requested), 'result': result, 'error': error})
    return result

def read_packet(process, count, before):
    ready, packets = threading.Event(), []
    def read():
        packets.append(process.stdout.read(count)); ready.set()
    threading.Thread(target=read, daemon=True).start()
    assert ready.wait(max(.001, 60 - (time.monotonic() - before))), 'readiness timeout'
    assert packets == ([b'ready'] if count == 5 else [b'rejected']), packets
    return packets[0]

def call(role, label, mode, root, request, expected=None, kind=None, mutate=None, recover=None, probe=False):
    guard()
    request_bytes = request if isinstance(request, bytes) else str(request).encode('utf-8')
    absolute = ''
    if not isinstance(request, bytes) and not probe:
        absolute = str(hostbuild._disk_absolute(Path(root) / str(request)))
        original = oracle(role + ':' + label, Path(absolute))
        if expected is None: expected = 2 if original == 'reject' else 0
        if kind is None: kind = original if original != 'reject' else 'none'
    else:
        expected = 2 if expected is None else expected
        kind = 'none' if kind is None else kind
    program = products / (role + '-source-stage.' + suffix)
    argv = [str(program), mode, str(root).encode('utf-8').hex(), request_bytes.hex(), absolute.encode('utf-8').hex(), kind]
    before = time.monotonic()
    process = subprocess.Popen(argv, cwd=ROOT, env={**os.environ, **sentinels}, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, preexec_fn=None if windows else limits)
    prefix = b''
    try:
        if mutate:
            prefix = read_packet(process, 5, before)
            mutate()
            if kind == 'none':
                final = oracle(role + ':' + label, Path(absolute), final=True)
                if expected is None: expected = 2 if final == 'reject' else 0
                assert expected == (2 if final == 'reject' else 0), (label, expected, final)
            if recover:
                process.stdin.write(b'v'); process.stdin.flush()
                prefix += read_packet(process, 8, before)
                recover()
                assert oracle(role + ':' + label + ':restored', Path(absolute), final=True) == 'none'
            stdout, stderr = process.communicate(input=b'v', timeout=max(.001, 60 - (time.monotonic() - before)))
        else:
            stdout, stderr = process.communicate(timeout=max(.001, 60 - (time.monotonic() - before)))
    except BaseException:
        process.kill(); process.communicate(); raise
    row = {'role': role, 'label': label, 'mode': mode, 'argv': argv, 'expected_exit': expected,
           'exit_code': process.returncode, 'stdout': (prefix + stdout).decode('ascii'), 'stderr': stderr.decode('utf-8'),
           'seconds': time.monotonic() - before, 'timeout_seconds': 60,
           'linux_address_space_bytes': None if windows else 33554432, 'linux_descriptor_limit': None if windows else 10240}
    records.append(row)
    (output / 'running.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    assert process.returncode == expected and stdout == (b'accept' if expected == 0 else b'reject') and stderr == b'', row
    assert row['seconds'] < 60
    guard()

try:
    guard(True)
    for role in ('native', 'checked'):
        for shape in ('present', 'empty', 'missing-leaf', 'missing-parents', 'absolute', 'dots', 'unicode', 'space', 'long-parent',
                      'parent-alias-present', 'parent-alias-missing', 'broken-parent', 'file-parent', 'directory', 'leaf-link', 'broken-leaf',
                      *(('colon', 'backslash', 'fifo', 'loop-parent') if not windows else ('unc-present', 'unc-missing'))):
            root = box(shape); request = 'payload.bin'
            if shape == 'empty': (root / request).write_bytes(b'')
            elif shape == 'missing-leaf': request = 'missing.bin'
            elif shape == 'missing-parents': request = 'ghost/deep/missing.bin'
            elif shape == 'absolute': request = str(root / 'ghost/deep/missing.bin')
            elif shape == 'dots': request = './ghost/../missing.bin'
            elif shape in ('unicode', 'space', 'colon', 'backslash'):
                request = {'unicode': 'missing-\u03b4-\u732b.bin', 'space': 'missing with spaces.bin', 'colon': 'colon:missing.bin', 'backslash': 'back\\missing.bin'}[shape]
            elif shape == 'long-parent':
                for depth in range(5): root /= 'part-' + str(depth) + '-' + 'x' * 60
                root.mkdir(parents=True); request = 'missing.bin'
            elif shape.startswith('parent-alias'):
                (root / 'actual').mkdir(); (root / 'actual/payload.bin').write_bytes(b'original')
                alias_make(root / 'alias', root / 'actual'); request = 'alias/' + ('payload.bin' if shape.endswith('present') else 'ghost/missing.bin')
            elif shape == 'broken-parent': alias_make(root / 'alias', root / 'unavailable'); request = 'alias/child.bin'
            elif shape == 'file-parent': request = 'payload.bin/child.bin'
            elif shape == 'directory': request = '.'
            elif shape in ('leaf-link', 'broken-leaf'):
                request = 'linked'; alias_make(root / request, root if shape == 'leaf-link' else root / 'unavailable')
            elif shape == 'fifo': request = 'pipe'; os.mkfifo(root / request)
            elif shape == 'loop-parent': alias_make(root / 'loop', root / 'loop'); request = 'loop/child.bin'
            elif shape.startswith('unc-'):
                root = Path('//localhost/C$' + root.as_posix()[2:]); request = 'payload.bin' if shape.endswith('present') else 'ghost/deep/missing.bin'
            call(role, shape, 'open', root, request)
        for present in (False, True):
            for mode in ('zero-quota', 'max-quota', 'one-quota', 'two-quota', 'null-root', 'null-request', 'null-output', 'null-view', 'null-owner-view', 'null-close'):
                root=box(mode); request='payload.bin' if present else 'ghost/deep/missing.bin'
                expected = 2 if mode in ('zero-quota','max-quota','null-root','null-request','null-output','null-view') or (present and mode in ('one-quota','two-quota')) else 0
                call(role, mode + ('-file' if present else '-none'), mode, root, request, expected=expected, kind='file' if present else 'none')
        for label, raw in [('overflow',b'x'*8192),('bad-continuation',b'bad-\xc2'),('overlong',b'bad-\xc0\x80'),('surrogate',b'bad-\xed\xa0\x80'),('above-scalar',b'bad-\xf4\x90\x80\x80')]:
            call(role,label,'open',box(label),raw,expected=2)
        for label, raw in [('relative-probe','relative/missing.bin'),('empty-probe',''),('null-probe','ignored'),('overflow-probe',b'/' + b'x'*8191),('utf8-probe',b'/bad-\xc2')]:
            call(role,label,'null-probe' if label=='null-probe' else 'probe','',raw,expected=2,probe=True)
        for mutation in ('unchanged','sibling','first-parent','complete-parent','file-parent','broken-parent','leaf-file','leaf-directory','leaf-link','recover'):
            root=box('missing-'+mutation); requested=root/'ghost/deep/missing.bin'
            def change(root=root, requested=requested, mutation=mutation):
                if mutation=='sibling': (root/'unrelated.bin').write_bytes(b'unrelated')
                elif mutation=='first-parent': (root/'ghost').mkdir()
                elif mutation=='complete-parent': requested.parent.mkdir(parents=True)
                elif mutation=='file-parent': (root/'ghost').write_bytes(b'not a directory')
                elif mutation=='broken-parent': alias_make(root/'ghost',root/'unavailable')
                elif mutation in ('leaf-file','recover'): requested.parent.mkdir(parents=True); requested.write_bytes(b'appeared')
                elif mutation=='leaf-directory': requested.mkdir(parents=True)
                elif mutation=='leaf-link': requested.parent.mkdir(parents=True); alias_make(requested,root/'unavailable')
            expected=2 if mutation in ('leaf-file','leaf-directory','leaf-link','recover') or (mutation=='file-parent' and not windows) else 0
            call(role,'missing-'+mutation,'hold-recover' if mutation=='recover' else 'hold',root,'ghost/deep/missing.bin',expected=expected,kind='none',mutate=change,
                 recover=(lambda requested=requested: requested.unlink()) if mutation=='recover' else None)
        for streamed in (False,True):
            for mutation in ('unchanged','sibling','edit','remove','restored-time'):
                root=box('present-'+mutation); path=root/'payload.bin';stamp=path.stat()
                def change(path=path,root=root,mutation=mutation,stamp=stamp):
                    if mutation=='sibling': (root/'unrelated.bin').write_bytes(b'unrelated')
                    elif mutation in ('edit','restored-time'):
                        path.write_bytes(b'modified')
                        if mutation=='restored-time': os.utime(path,ns=(stamp.st_atime_ns,stamp.st_mtime_ns))
                    elif mutation=='remove':
                        if windows and streamed:
                            try: path.unlink()
                            except PermissionError as error:
                                assert error.winerror == 32 and path.read_bytes() == b'original'
                                delete_controls.append({'path':str(path),'winerror':error.winerror,'payload_unchanged':True})
                            else: raise AssertionError('Windows retained file unexpectedly allowed deletion')
                        else: path.unlink()
                expected=2 if mutation=='edit' or (mutation=='remove' and not (windows and streamed)) or (streamed and mutation=='restored-time') else 0
                call(role,'present-'+mutation+('-stream' if streamed else '-metadata'),'hold-stream' if streamed else 'hold',root,'payload.bin',expected=expected,kind='file',mutate=change)
    guard(True)
    receipt.update(status='pass',calls=len(records),positive_calls=sum(row['expected_exit']==0 for row in records),negative_calls=sum(row['expected_exit']==2 for row in records),actual_python_oracles=len(oracles))
except BaseException as error:
    receipt.update(status='fail',error=repr(error));traceback.print_exc()
receipt['seconds']=time.monotonic()-started
(output/'closed.json').write_text(json.dumps(receipt,indent=2,sort_keys=True)+'\n',encoding='utf-8',newline='\n')
print(json.dumps({key:receipt.get(key) for key in ('status','host','calls','positive_calls','negative_calls','actual_python_oracles','seconds','error')}))
raise SystemExit(0 if receipt['status']=='pass' else 1)

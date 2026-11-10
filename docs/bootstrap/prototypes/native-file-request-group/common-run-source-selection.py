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
products = BASE / ('common-products-' + host + '1')
build_bytes = (products / 'closed.json').read_bytes()
build = json.loads(build_bytes)
assert build['status'] == 'pass'
output = BASE / ('common-input-selection-contracts-' + host + '1')
assert not output.exists()
output.mkdir()
trees = output / 'trees' if windows else Path(tempfile.mkdtemp(prefix='cupid-source-selection-', dir='/var/tmp'))
trees.mkdir(exist_ok=True)
identity = lambda data: {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
state = {'schema': 'cupid.native-source-selection-contracts.v1', 'status': 'running', 'host': host,
         'build': identity(build_bytes), 'fixture': build['sources']['source_selection_contract_eighth.cc'], 'runner': identity(Path(__file__).read_bytes()),
         'producer_sentinels': build['producer_sentinels'], 'timeout_seconds': 60,
         'linux_address_space_bytes': None if windows else 33554432,
         'linux_descriptor_limit': None if windows else 10240, 'native_fixture_root': str(trees), 'records': []}
started = time.monotonic()

def guard():
    assert (products / 'closed.json').read_bytes() == build_bytes
    assert identity((BASE / 'common-source' / 'source_selection_contract_eighth.cc').read_bytes()) == build['sources']['source_selection_contract_eighth.cc']
    for name, facts in build['sources'].items(): assert identity((BASE / 'common-source' / name).read_bytes()) == facts
    for name, facts in build['normal_source_inputs'].items(): assert identity((ROOT / name).read_bytes()) == facts
    for role in ('native', 'checked'):
        assert identity((products / (role + '-input-selection.' + suffix)).read_bytes()) == build[role + '_images']['input-selection']

def bounds():
    import resource
    resource.setrlimit(resource.RLIMIT_AS, (33554432, 33554432))
    resource.setrlimit(resource.RLIMIT_NOFILE, (10240, 10240))

def run(role, namespace, family, label, expected=0, quota=64, request='payload.bin', prepare=None, mutate=None, mode=None):
    guard()
    case = '-'.join((role, namespace, family, label))
    container = trees / case
    working = container / 'working \u03b4-\u732b'
    working.mkdir(parents=True)
    if namespace == 'unc': working = Path('//localhost/C$' + working.as_posix()[2:])
    (working / 'payload.bin').write_bytes(b'original-payload')
    (working / 'output.bin').write_bytes(b'previous-output')
    writer_bytes = (products / (role + '-input-selection.' + suffix)).read_bytes()
    (working / 'writer').write_bytes(writer_bytes)
    assert (working / 'writer').read_bytes() == writer_bytes
    root = working
    if prepare: root = prepare(working) or working
    mode = mode or family + ('-hold' if mutate else '')
    if windows and namespace == 'unc' and mode.endswith('-publish'): expected = 2
    argv = [str(products / (role + '-input-selection.' + suffix)), mode,
            str(root).encode('utf-8').hex(), request.encode('utf-8').hex(), str(quota)]
    row = {'role': role, 'namespace': namespace, 'family': family, 'label': label, 'argv': argv,
           'expected_exit': expected, 'timeout_seconds': 60, 'linux_address_space_bytes': None if windows else 33554432,
           'linux_descriptor_limit': None if windows else 10240}
    state['records'].append(row)
    before = time.monotonic()
    process = subprocess.Popen(argv, cwd=ROOT, env={**os.environ, **build['producer_sentinels']},
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                               preexec_fn=None if windows else bounds)
    prefix = b''
    try:
        if mode.endswith("-publish") and expected != 2:
            packets = queue.Queue()
            threading.Thread(target=lambda: packets.put(process.stdout.readline()), daemon=True).start()
            try: prefix = packets.get(timeout=max(.001, 60 - (time.monotonic() - before)))
            except queue.Empty: raise TimeoutError("C candidate handshake exceeded sixty seconds")
            assert prefix.startswith(b"candidate:")
            candidate = Path(prefix[len(b"candidate:"):].rstrip(b"\r\n").decode("utf-8"))
            requested_candidate = str(candidate)
            if not windows and requested_candidate.startswith('/proc/self/fd/'):
                descriptor = requested_candidate[len('/proc/self/fd/'):]
                assert descriptor.isdecimal(), (case, requested_candidate)
                candidate = Path('/proc') / str(process.pid) / 'fd' / descriptor
            if windows:
                resolved_candidate = candidate.parent.resolve(strict=True) / candidate.name
                assert not candidate.is_symlink() and (not candidate.exists() or candidate.is_file()), (case, str(candidate))
            else:
                resolved_candidate = candidate.resolve(strict=True)
                assert candidate.is_file(), (case, str(candidate))
            assert resolved_candidate.is_relative_to(working.resolve(strict=True)), (case, str(candidate), str(resolved_candidate))
            row['candidate_request'] = requested_candidate
            row['candidate_resolved'] = str(resolved_candidate)
            row["candidate"] = {"path": str(candidate), "before_runner": identity(candidate.read_bytes()) if candidate.exists() else None, "writer": "guarded frozen self-emitter"}
            process.stdin.write(b"x\n")
            process.stdin.flush()
        if mutate:
            packets = queue.Queue()
            threading.Thread(target=lambda: packets.put(process.stdout.readline()), daemon=True).start()
            try: prefix = packets.get(timeout=max(.001, 60 - (time.monotonic() - before)))
            except queue.Empty: raise TimeoutError('C process handshake exceeded sixty seconds')
            assert prefix.rstrip(b'\r\n') == b'ready', (case, prefix)
            mutate(working)
            process.stdin.write(b'x\n')
            process.stdin.flush()
        stdout, stderr = process.communicate(timeout=max(.001, 60 - (time.monotonic() - before)))
        raw = prefix + stdout
        row.update(exit_code=process.returncode, seconds=time.monotonic() - before, stdout=identity(raw), stderr=identity(stderr),
                   raw_stdout_base64=base64.b64encode(raw).decode('ascii'))
        (output / (case + '-stdout.bin')).write_bytes(raw)
        (output / (case + '-stderr.bin')).write_bytes(stderr)
        assert process.returncode == expected and stdout.replace(b'\r\n', b'\n') == (b'accept\n' if expected == 0 else (b'preflight-reject\n' if expected == 2 else b'reject\n')) and not stderr and row['seconds'] < 60, (case, process.returncode, stdout, stderr)
        public = (working / 'output.bin').read_bytes()
        assert public == (b'new-candidate' if mode == 'root-ordinary-publish' and expected == 0 else b'previous-output'), (case, public)
        row['public_output'] = identity(public)
        row['publication_rejection_checks'] = 4 if mode.endswith('-publish') and mode != 'root-ordinary-publish' and expected == 0 else 0
        row['legacy_transaction_preflight_rejection'] = expected == 2
        assert not list(working.glob('.cupid-*')), case
    except BaseException as error:
        process.kill()
        stdout, stderr = process.communicate()
        row.update(error=repr(error), exit_code=process.returncode, seconds=time.monotonic() - before)
        (output / (case + '-failure-stdout.bin')).write_bytes(prefix + stdout)
        (output / (case + '-failure-stderr.bin')).write_bytes(stderr)
        raise
    finally: (output / 'running.json').write_text(json.dumps(state, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    guard()

def local_fixture(path):
    value = str(path)
    prefix = "\\\\localhost\\C$\\"
    if windows and value.lower().startswith(prefix.lower()):
        return Path("C:\\" + value[len(prefix):])
    return Path(path)

def fixture_symlink(source, destination, target_is_directory=False):
    os.symlink(local_fixture(source), local_fixture(destination), target_is_directory=target_is_directory)

def fixture_link(source, destination):
    os.link(local_fixture(source), local_fixture(destination))

def aliased(working):
    alias = working.parent / 'alias'
    other = working.parent / 'other'
    other.mkdir()
    fixture_link(working / 'payload.bin', other / 'payload.bin')
    fixture_symlink(working, alias, target_is_directory=True)
    return alias

def rebind(working):
    alias = working.parent / 'alias'
    if windows: os.rmdir(local_fixture(alias))
    else: alias.unlink()
    fixture_symlink(working.parent / 'other', alias, target_is_directory=True)

try:
    guard()
    for role in ('native', 'checked'):
        for namespace in ('drive', 'unc') if windows else ('posix',):
            for family in ('root', 'file', 'optional', 'stage'):
                run(role, namespace, family, 'copied-arguments')
                for quota in (0, 'max'): run(role, namespace, family, 'quota-' + str(quota), 1, quota)
                run(role, namespace, family, 'null-owner', 1, mode=family + '-null-owner')
                if family != 'root':
                    for invalid in ('null-view', 'null-root', 'null-request'):
                        run(role, namespace, family, invalid, expected=1, mode=family + '-' + invalid)
                run(role, namespace, family, 'publication-denied', mode=family + '-publish')
                run(role, namespace, family, 'metadata-edited', 1,
                    mutate=lambda w: (w / 'payload.bin').write_bytes(b'changed-size'))
                run(role, namespace, family, 'parent-alias', prepare=aliased)
                run(role, namespace, family, 'hardlinked-parent-rebound', 1, prepare=aliased, mutate=rebind)
                if family != 'root':
                    run(role, namespace, family, 'insufficient-quota', 1, 1)
                    run(role, namespace, family, 'directory-leaf', 1, request='directory',
                        prepare=lambda w: (w / 'directory').mkdir())
                    run(role, namespace, family, 'linked-leaf', 1, request='linked.bin',
                        prepare=lambda w: fixture_symlink(w / 'payload.bin', w / 'linked.bin'))
                    run(role, namespace, family, 'missing-leaf', 1 if family == 'file' else 0, request='missing.bin')
                if family in ('root', 'optional', 'stage'):
                    run(role, namespace, family, 'sibling-write', mutate=lambda w: (w / 'sibling.bin').write_bytes(b'allowed'))
            run(role, namespace, 'root', 'single-entry-quota', quota=1, mode='root-only')
            run(role, namespace, 'root', 'null-physical', 1, mode='root-null-physical')
            run(role, namespace, 'optional', 'null-kind', 1, mode='optional-null-kind')
            run(role, namespace, 'root', 'begin-copied-arguments', mode='root-begin')
            for quota in (0, 'max'):
                run(role, namespace, 'root', 'begin-quota-' + str(quota), 1, quota, mode='root-begin')
            for invalid in ('null-root', 'null-owner', 'null-physical'):
                run(role, namespace, 'root', 'begin-' + invalid, 1, mode='root-begin-' + invalid)
            run(role, namespace, 'root', 'begin-publication-denied', mode='root-begin-publish')
            run(role, namespace, 'root', 'begin-one-root', quota=1, mode='root-begin-only')
            run(role, namespace, 'root', 'begin-parent-alias', prepare=aliased, mode='root-begin')
            run(role, namespace, 'root', 'begin-alias-rebind', 1, prepare=aliased, mutate=rebind, mode='root-begin-hold')
            run(role, namespace, 'root', 'begin-metadata-edited', 1,
                mutate=lambda w: (w / 'payload.bin').write_bytes(b'changed-payload-longer'), mode='root-begin-hold')
            run(role, namespace, 'root', 'ordinary-publication-control', mode='root-ordinary-publish')
            for family, expected in (('optional', 1), ('stage', 0)):
                run(role, namespace, family, 'missing-parent-appears', expected, request='ghost/child.bin',
                    mutate=lambda w: (w / 'ghost').mkdir())
    state.update(status='pass', calls=len(state['records']), positive_calls=sum(r['expected_exit'] == 0 for r in state['records']),
                 negative_calls=sum(r['expected_exit'] == 1 for r in state['records']),
                 publication_rejection_checks=sum(r['publication_rejection_checks'] for r in state['records']),
                 legacy_transaction_preflight_rejections=sum(r['expected_exit'] == 2 for r in state['records']))
except BaseException as error:
    state.update(status='fail', error=repr(error))
    traceback.print_exc()
state['seconds'] = time.monotonic() - started
(output / 'closed.json').write_text(json.dumps(state, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
print(json.dumps({k: state.get(k) for k in ('status', 'host', 'calls', 'positive_calls', 'negative_calls', 'publication_rejection_checks', 'seconds', 'error')}))
raise SystemExit(0 if state['status'] == 'pass' else 1)

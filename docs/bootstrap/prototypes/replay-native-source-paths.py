"""Rebuild the held image source spelling component and compare it with actual Python pathname rules."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import traceback

created_output = None


def identity(data):
    return {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def extract_sources(patch, destination, expected):
    allowed = {
        'b/toolchain/cupidbuild_source_path.cc': 'cupidbuild_source_path.cc',
        'b/toolchain/cupidbuild_source_path.h': 'cupidbuild_source_path.h',
        'b/toolchain/prototypes/native_source_path/source_path_contract.cc': 'source_path_contract.cc',
    }
    lines = patch.decode('ascii').splitlines(keepends=True)
    position = 0
    seen = set()
    while position < len(lines):
        if lines[position] != '--- /dev/null\n' or not lines[position + 1].startswith('+++ '):
            raise ValueError('Unexpected source patch header')
        target = lines[position + 1][4:].strip()
        if target not in allowed or target in seen:
            raise ValueError('Unexpected or repeated source patch file')
        seen.add(target)
        match = re.fullmatch(r'@@ -0,0 \+1,(\d+) @@\n', lines[position + 2])
        if not match:
            raise ValueError('Unexpected new source hunk')
        count = int(match[1])
        start = position + 3
        body = lines[start:start + count]
        if len(body) != count or any(not line.startswith('+') for line in body):
            raise ValueError('Incomplete new source hunk')
        data = ''.join(line[1:] for line in body).encode('ascii')
        name = allowed[target]
        if identity(data) != expected[name]:
            raise ValueError('Held source identity differs: ' + name)
        (destination / name).write_bytes(data)
        position = start + count
    if seen != set(allowed):
        raise ValueError('Held source patch is incomplete')


def main():
    global created_output
    arguments = argparse.ArgumentParser(description=__doc__)
    arguments.add_argument('--output', type=Path, required=True)
    arguments.add_argument('--observer-products', type=Path, required=True)
    args = arguments.parse_args()
    artifact = Path(__file__).resolve().parent
    root = artifact.parents[2]
    output = args.output.resolve()
    baseline = args.observer_products.resolve()
    if output.parent != root / 'build' or output.exists():
        raise ValueError('Use a fresh direct child of the repository build directory')
    if not baseline.is_relative_to(root / 'build') or baseline == output:
        raise ValueError('Use retained observer products below the repository build directory')
    output.mkdir(parents=True)
    created_output = output
    proof_path = artifact.parent / 'evidence/native-source-paths-20261010.json'
    observer_path = artifact.parent / 'evidence/native-observer-paths-20261010.json'
    proof_bytes, observer_bytes = proof_path.read_bytes(), observer_path.read_bytes()
    proof, observer = json.loads(proof_bytes), json.loads(observer_bytes)
    if proof['status'] != 'pass' or observer['status'] != 'pass' or identity(observer_bytes) != proof['observer_prerequisite']:
        raise ValueError('The accepted source or observer prerequisite differs')
    patch_path = artifact / 'native-source-path.patch'
    patch = patch_path.read_bytes()
    if identity(patch) != proof['artifact_patch']:
        raise ValueError('The held source patch differs')
    for name, facts in proof['replay_helpers'].items():
        data = (artifact / 'native-source-path' / name).read_bytes()
        if identity(data) != facts:
            raise ValueError('Replay helper differs: ' + name)
        (output / name).write_bytes(data)
    spec = importlib.util.spec_from_file_location('consumer_audit_common', output / 'consumer_audit_common.py')
    common = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(common)
    windows = os.name == 'nt'
    host = 'windows' if windows else 'linux'
    captured = {proof_path: proof_bytes, observer_path: observer_bytes, patch_path: patch}
    for name, facts in proof['source_inputs'].items():
        path = root / name
        data = common.read(path)
        if identity(data) != facts or data != common.git_bytes(proof['source_revision'], name):
            raise ValueError('Committed normal producer input differs: ' + name)
        captured[path] = data
    if len(proof['source_inputs']) != 99:
        raise ValueError('The normal producer inventory is incomplete')
    for name in common.SEED_FILES:
        path = root / name
        data = common.read(path)
        if data != common.git_bytes('HEAD', name):
            raise ValueError('An installed seed file differs from the current commit: ' + name)
        captured[path] = data
    baseline_path = baseline / 'closed.json'
    baseline_bytes = common.read(baseline_path)
    baseline_record = json.loads(baseline_bytes)
    replay_path = baseline.parent / 'closed.json'
    replay_bytes = common.read(replay_path)
    replay = json.loads(replay_bytes)
    if replay['status'] != 'pass' or replay['schema'] != 'cupid.native-observer-paths-replay.v1' or replay['host'] != host:
        raise ValueError('The complete observer replay did not pass on this host')
    if (replay['retained_api_calls'], replay['missing_unc_api_calls'], replay['resolved_parent_api_calls'],
        replay['legacy_selected_methods'], replay['alias_selected_methods']) != ((92, 104, 22, 176, 24) if windows else (86, 64, 20, 176, 24)):
        raise ValueError('The complete observer selections differ')
    if baseline_record['status'] != 'pass' or baseline_record['sources'] != observer['prototype_sources']:
        raise ValueError('The complete observer build or source copy differs')
    if baseline_record['objects'] != observer['hosts'][host]['objects']:
        raise ValueError('The observer object inventory differs')
    expected_images = {name: facts for name, facts in observer['hosts'][host]['images'].items() if name.startswith('checked-')}
    expected_roles = {name.removeprefix('checked-').removesuffix('.exe' if windows else '.elf'): facts
        for name, facts in expected_images.items()}
    if baseline_record['checked_images'] != expected_roles:
        raise ValueError('The observer program inventory differs')
    captured[baseline_path], captured[replay_path] = baseline_bytes, replay_bytes
    for name, facts in baseline_record['sources'].items():
        path = baseline / 'source' / name
        data = common.read(path)
        if identity(data) != facts:
            raise ValueError('Retained observer source differs: ' + name)
        captured[path] = data
    for name, facts in baseline_record['objects'].items():
        path = baseline / 'objects' / name
        data = common.read(path)
        if identity(data) != facts:
            raise ValueError('Retained observer object differs: ' + name)
        captured[path] = data
    for name, facts in expected_images.items():
        path = baseline / name
        data = common.read(path)
        if identity(data) != facts:
            raise ValueError('Retained observer program differs: ' + name)
        captured[path] = data
    for name in ('tools/hostbuild.py', 'tools/bootstrap_toolchain.py'):
        path = root / name
        captured[path] = common.read(path)
    sources = output / 'source'
    sources.mkdir()
    extract_sources(patch, sources, proof['prototype_sources'])
    for name, facts in proof['replay_helpers'].items():
        captured[output / name] = common.read(output / name)
    selection_path = output / 'observer-products.json'
    selection_path.write_text(json.dumps({'directory': str(baseline)}, indent=2) + '\n', encoding='utf-8', newline='\n')
    captured[selection_path] = common.read(selection_path)
    sys.path.insert(0, str(root))
    from tools import bootstrap_toolchain as bootstrap
    seed = bootstrap.freeze_seed_inputs(root / ('bootstrap/seeds/i386-' + host + '/manifest.json'), output / 'seed')
    for path in seed.tools.values():
        signature = path.read_bytes()[:4]
        if not (signature[:2] == b'MZ' if windows else signature == b'\x7fELF'):
            raise ValueError('Use the recognized native-host installed seed')
    sentinels = {name: 'forbidden-host-' + name for name in ('CC', 'CXX', 'CPP', 'AS', 'AR', 'LD', 'NM', 'OBJCOPY', 'NASM')}
    receipt = {'schema': 'cupid.native-source-path-replay.v1', 'status': 'running', 'host': host,
        'runner': identity(Path(__file__).read_bytes()), 'commands': [], 'producer_sentinels': sentinels,
        'seed_manifest': identity(seed.manifest_bytes), 'tools': {name: identity(path.read_bytes()) for name, path in seed.tools.items()},
        'artifact_patch': identity(patch), 'source_revision': proof['source_revision'],
        'source_snapshot_sha256': proof['source_snapshot_sha256'], 'observer_build': identity(baseline_bytes),
        'observer_complete_replay': identity(replay_bytes), 'captured_controls': {str(path): identity(data) for path, data in captured.items()}}
    started = time.monotonic()

    def guard():
        bootstrap.require_live_seed_inputs(seed)
        for path, data in captured.items():
            if common.read(path) != data:
                raise ValueError('Input changed during replay: ' + str(path))

    try:
        for label, script, arguments in (
            ('oracles', 'capture-path-oracles.py', ['--output', output / 'oracles']),
            ('build', 'build-source-path.py', [output / ('products-' + host + '1')]),
            ('contracts', 'run-source-path.py', ['1', '1']),
            ('grammar', 'run-source-path-grammar.py', ['1', '1'])):
            guard()
            before = time.monotonic()
            result = subprocess.run([sys.executable, str(output / script), *map(str, arguments)], cwd=root,
                env={**os.environ, **sentinels}, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            (output / (label + '-stdout.bin')).write_bytes(result.stdout)
            (output / (label + '-stderr.bin')).write_bytes(result.stderr)
            record = {'label': label, 'argv': [sys.executable, str(output / script), *map(str, arguments)],
                'exit_code': result.returncode, 'seconds': time.monotonic() - before,
                'stdout': identity(result.stdout), 'stderr': identity(result.stderr)}
            receipt['commands'].append(record)
            (output / 'running.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
            if result.returncode != 0:
                raise ValueError(record)
            guard()
        builds = json.loads((output / ('products-' + host + '1/closed.json')).read_bytes())
        contracts = json.loads((output / ('contracts-' + host + '1/closed.json')).read_bytes())
        grammar = json.loads((output / ('grammar-' + host + '1/closed.json')).read_bytes())
        oracles = json.loads((output / 'oracles/closed.json').read_bytes())
        if any(record['status'] != 'pass' for record in (builds, contracts, grammar, oracles)):
            raise ValueError('A child receipt did not close successfully')
        if builds['sources'] != proof['prototype_sources'] or builds['objects'] != proof['hosts'][host]['objects']:
            raise ValueError('The complete source or checked object copies differ')
        if builds['programs']['checked'] != proof['hosts'][host]['programs']['checked']:
            raise ValueError('The complete checked program differs')
        if contracts['calls'] != (86 if windows else 92) or grammar['calls'] != (126 if windows else 50):
            raise ValueError('The complete runtime selections differ')
        rows = contracts['records'] + grammar['records']
        negative = sum(row['expected_exit_code'] != 0 for row in rows)
        if negative != 20 or any(row['status'] != 'pass' for row in rows):
            raise ValueError('The complete runtime outcomes differ')
        for name in common.SEED_FILES:
            if common.read(root / name) != common.git_bytes('HEAD', name):
                raise ValueError('Installed seed changed in the current commit: ' + name)
        guard()
        receipt.update(status='pass', calls=len(rows), positive_calls=len(rows) - negative, negative_calls=negative,
            actual_oracle_cases_per_caller=len(oracles['records']), normal_source_inputs_unchanged=99,
            installed_root_seed_files_unchanged=15, complete_checked_products_equal_original=True,
            objects=builds['objects'], checked_program=builds['programs']['checked'])
    except BaseException as error:
        receipt.update(status='fail', error=repr(error))
        traceback.print_exc()
    receipt['seconds'] = time.monotonic() - started
    (output / 'closed.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    print(json.dumps({key: receipt.get(key) for key in ('status', 'host', 'calls', 'positive_calls', 'negative_calls', 'seconds', 'error')}))
    return 0 if receipt['status'] == 'pass' else 1


if __name__ == '__main__':
    try:
        result = main()
    except BaseException as error:
        traceback.print_exc()
        if created_output is not None and not (created_output / 'closed.json').exists():
            (created_output / 'closed.json').write_text(json.dumps({'schema': 'cupid.native-source-path-replay.v1',
                'status': 'fail', 'stage': 'preflight', 'error': repr(error)}, indent=2) + '\n', encoding='utf-8', newline='\n')
        result = 1
    raise SystemExit(result)

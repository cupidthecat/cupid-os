"""Rebuild and replay required source-file owners and the complete root prerequisite."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import traceback

created_output = None


def identity(data):
    return {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def apply_sources(patch_bytes, root, destination, expected):
    """Apply only the five reviewed files, checking each context and line count."""
    allowed = {
        'b/toolchain/prototypes/native_source_file/cupidbuild_source_path.cc': None,
        'b/toolchain/prototypes/native_source_file/cupidbuild_source_path.h': None,
        'b/toolchain/prototypes/native_source_file/cupidbuild_source_file.cc': None,
        'b/toolchain/prototypes/native_source_file/cupidbuild_source_file.h': None,
        'b/toolchain/prototypes/native_source_file/source_file_contract.cc': None,
    }
    lines = patch_bytes.decode('ascii').splitlines(keepends=True)
    position = 0
    seen = set()
    while position < len(lines):
        old_header, new_header = lines[position:position + 2]
        if not old_header.startswith('--- ') or not new_header.startswith('+++ '):
            raise ValueError('Unexpected source patch header')
        new_name = new_header[4:].strip()
        if new_name not in allowed or new_name in seen:
            raise ValueError('Unexpected or repeated source patch file')
        seen.add(new_name)
        source_name = allowed[new_name]
        if old_header[4:].strip() != ('a/' + source_name if source_name else '/dev/null'):
            raise ValueError('Unexpected source patch base')
        before = (root / source_name).read_bytes().decode('ascii').splitlines(keepends=True) if source_name else []
        after = []
        cursor = 0
        position += 2
        while position < len(lines) and not lines[position].startswith('--- '):
            match = re.fullmatch(r'@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@[^\n]*\n', lines[position])
            if not match:
                raise ValueError('Unexpected source patch hunk')
            old_start, old_count = int(match[1]), int(match[2] or 1)
            new_start, new_count = int(match[3]), int(match[4] or 1)
            start = old_start - 1 if old_count else old_start
            if not cursor <= start <= len(before):
                raise ValueError('Source patch hunks overlap or exceed the base')
            after.extend(before[cursor:start])
            if len(after) != (new_start - 1 if new_count else new_start):
                raise ValueError('Source patch new position differs')
            cursor = start
            position += 1
            removed = added = 0
            while removed < old_count or added < new_count:
                line = lines[position]
                position += 1
                if line == '\n':
                    line = ' \n'
                marker, value = line[0], line[1:]
                if marker in (' ', '-'):
                    if cursor >= len(before) or before[cursor] != value:
                        raise ValueError('Source patch base context differs')
                    cursor += 1
                    removed += 1
                if marker in (' ', '+'):
                    after.append(value)
                    added += 1
                if marker not in (' ', '-', '+') or removed > old_count or added > new_count:
                    raise ValueError('Source patch line counts differ')
            if removed != old_count or added != new_count:
                raise ValueError('Source patch hunk is incomplete')
        after.extend(before[cursor:])
        data = ''.join(after).encode('ascii')
        name = Path(new_name).name
        if identity(data) != expected[name]:
            raise ValueError('Held source identity differs: ' + name)
        (destination / name).write_bytes(data)
    if seen != set(allowed):
        raise ValueError('Held source patch is incomplete')


def main():
    global created_output
    arguments = argparse.ArgumentParser(description=__doc__)
    arguments.add_argument('--output', type=Path, required=True)
    arguments.add_argument('--root-prerequisite', type=Path, help='Reuse a complete accepted root replay in this repository build directory')
    args = arguments.parse_args()
    artifact = Path(__file__).resolve().parent
    root = artifact.parents[2]
    output = args.output.resolve()
    root_output = args.root_prerequisite.resolve() if args.root_prerequisite else output.with_name(output.name + '-roots')
    if output.parent != root / 'build' or output.exists():
        raise ValueError('Use a fresh direct child of build')
    if root_output.parent != root / 'build' or root_output.is_symlink() or (not args.root_prerequisite and root_output.exists()):
        raise ValueError('Use a fresh -roots sibling or an explicit complete root replay within this repository build directory')
    output.mkdir(parents=True)
    created_output = output
    proof_path = artifact.parent / 'evidence/native-source-file-20261010.json'
    proof_bytes = proof_path.read_bytes()
    proof = json.loads(proof_bytes)
    root_proof_path = artifact.parent / 'evidence/native-source-root-20261010.json'
    root_proof_bytes = root_proof_path.read_bytes()
    root_proof = json.loads(root_proof_bytes)
    if proof['status'] != 'pass' or root_proof['status'] != 'pass' or identity(root_proof_bytes) != proof['root_prerequisite']:
        raise ValueError('Complete original file and root acceptances are required')
    patch_path = artifact / 'native-source-file.patch'
    patch_bytes = patch_path.read_bytes()
    if identity(patch_bytes) != proof['artifact_patch']:
        raise ValueError('File-owner source patch differs from reviewed evidence')
    captured = {proof_path: proof_bytes, root_proof_path: root_proof_bytes, patch_path: patch_bytes}
    lexical_proof_path = artifact.parent / 'evidence/native-source-paths-20261010.json'
    lexical_bytes = lexical_proof_path.read_bytes()
    lexical = json.loads(lexical_bytes)
    if identity(lexical_bytes) != proof['lexical_prerequisite'] or lexical['status'] != 'pass':
        raise ValueError('Accepted lexical prerequisite differs')
    for name in ('cupidbuild_source_path.cc', 'cupidbuild_source_path.h'):
        if proof['prototype_sources'][name] != lexical['prototype_sources'][name]:
            raise ValueError('Accepted lexical source differs: ' + name)
    captured[lexical_proof_path] = lexical_bytes
    for name, facts in proof['replay_helpers'].items():
        path = artifact / 'native-source-file' / name
        data = path.read_bytes()
        if identity(data) != facts: raise ValueError('File replay helper differs: ' + name)
        captured[path] = data
        (output / name).write_bytes(data)
    root_artifacts = {
        artifact / 'replay-native-source-root.py': root_proof['replay_runner'],
        artifact / 'native-source-root.patch': root_proof['artifact_patch'],
        **{artifact / 'native-source-root' / name: facts for name, facts in root_proof['replay_helpers'].items()},
    }
    for path, facts in root_artifacts.items():
        data = path.read_bytes()
        if identity(data) != facts: raise ValueError('Root replay prerequisite differs: ' + str(path))
        captured[path] = data
    spec = importlib.util.spec_from_file_location('consumer_audit_common', output / 'consumer_audit_common.py')
    common = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(common)
    if proof['source_inputs'] != root_proof['source_inputs'] or len(proof['source_inputs']) != 99:
        raise ValueError('Normal producer inventory differs')
    for name, facts in proof['source_inputs'].items():
        path = root / name
        data = common.read(path)
        if identity(data) != facts or data != common.git_bytes(proof['source_revision'], name):
            raise ValueError('Committed normal input differs: ' + name)
        captured[path] = data
    for name, facts in {**root_proof['legacy_test_sources'], **root_proof['support_inputs']}.items():
        path = root / name
        data = common.read(path)
        if identity(data) != facts: raise ValueError('Root support input differs: ' + name)
        captured[path] = data
    for name in common.SEED_FILES:
        path = root / name
        data = common.read(path)
        if data != common.git_bytes('HEAD', name): raise ValueError('Installed seed differs from the current commit: ' + name)
        captured[path] = data
    sys.path.insert(0, str(root))
    from tools import bootstrap_toolchain as bootstrap
    windows = os.name == 'nt'
    host = 'windows' if windows else 'linux'
    suffix = 'exe' if windows else 'elf'
    seed = bootstrap.freeze_seed_inputs(root / ('bootstrap/seeds/i386-' + host + '/manifest.json'), output / 'seed')
    for path in seed.tools.values():
        signature = path.read_bytes()[:4]
        if not (signature[:2] == b'MZ' if windows else signature == b'\x7fELF'):
            raise ValueError('Use a recognized native host seed manifest')
    sentinels = {name: 'forbidden-host-' + name for name in ('CC', 'CXX', 'CPP', 'AS', 'AR', 'LD', 'NM', 'OBJCOPY', 'NASM')}
    receipt = {'schema': 'cupid.native-source-file-replay.v1', 'status': 'running', 'host': host,
        'runner': identity(Path(__file__).read_bytes()), 'commands': [], 'producer_sentinels': sentinels,
        'seed_manifest': identity(seed.manifest_bytes), 'artifact_patch': identity(patch_bytes),
        'tools': {name: identity(path.read_bytes()) for name, path in seed.tools.items()},
        'source_revision': proof['source_revision'], 'source_snapshot_sha256': proof['source_snapshot_sha256'],
        'root_output': str(root_output)}
    started = time.monotonic()

    def guard():
        bootstrap.require_live_seed_inputs(seed)
        for path, data in captured.items():
            if common.read(path) != data: raise ValueError('Input changed during replay: ' + str(path))

    def run(label, script, script_args):
        guard()
        before = time.monotonic()
        result = subprocess.run([sys.executable, str(script), *map(str, script_args)], cwd=root,
            env={**os.environ, **sentinels}, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        (output / (label + '-stdout.bin')).write_bytes(result.stdout)
        (output / (label + '-stderr.bin')).write_bytes(result.stderr)
        record = {'label': label, 'exit_code': result.returncode, 'seconds': time.monotonic() - before,
            'stdout': identity(result.stdout), 'stderr': identity(result.stderr)}
        receipt['commands'].append(record)
        (output / 'running.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
        if result.returncode != 0: raise ValueError(record)
        guard()

    def reuse_root():
        guard()
        before = time.monotonic()
        data = common.read(root_output / 'closed.json')
        record = json.loads(data)
        if record['status'] != 'pass' or record['host'] != host or record['runner'] != root_proof['replay_runner']:
            raise ValueError('The reused root replay must be complete and use the exact accepted runner')
        labels = ['build', 'source-root', 'source-profile', 'missing-unc', 'resolved-parent', 'contracts', 'legacy', 'alias']
        if [row['label'] for row in record['commands']] != labels or any(row['exit_code'] != 0 for row in record['commands']):
            raise ValueError('The reused root replay must pass every original selection')
        if record['objects'] != root_proof['hosts'][host]['objects'] or record['checked_images'] != root_proof['hosts'][host]['checked_images']:
            raise ValueError('The reused whole root products differ')
        if record['seed_manifest'] != identity(seed.manifest_bytes) or record['tools'] != receipt['tools']:
            raise ValueError('The reused root tool cohort differs')
        if record['source_revision'] != proof['source_revision'] or record['source_snapshot_sha256'] != proof['source_snapshot_sha256']:
            raise ValueError('The reused root source cohort differs')
        retained = {root_output / 'closed.json': data}
        for row in record['commands']:
            for stream in ('stdout', 'stderr'):
                path = root_output / (row['label'] + '-' + stream + '.bin')
                value = common.read(path)
                if identity(value) != row[stream]: raise ValueError('The reused original command stream differs')
                retained[path] = value
        groups = ['products', 'root-contracts', 'source-contracts', 'missing-unc', 'resolved-parent', 'contracts', 'legacy', 'alias']
        for group in groups:
            path = root_output / (group + '-' + host + '1') / 'closed.json'
            value = common.read(path)
            if json.loads(value)['status'] != 'pass': raise ValueError('A reused root child did not pass')
            retained[path] = value
        for name, facts in root_proof['replay_helpers'].items():
            path = root_output / name
            value = common.read(path)
            if identity(value) != facts: raise ValueError('A reused root helper differs')
            retained[path] = value
        products = root_output / ('products-' + host + '1')
        for name, facts in root_proof['prototype_sources'].items():
            path = products / 'source' / name
            value = common.read(path)
            if identity(value) != facts: raise ValueError('A reused root source differs')
            retained[path] = value
        for name, facts in record['objects'].items():
            path = products / 'objects' / name
            value = common.read(path)
            if identity(value) != facts: raise ValueError('A reused complete root object differs')
            retained[path] = value
        for name, facts in record['checked_images'].items():
            path = products / ('checked-' + name + '.' + suffix)
            value = common.read(path)
            if identity(value) != facts: raise ValueError('A reused complete root program differs')
            retained[path] = value
        alias = json.loads(retained[root_output / ('alias-' + host + '1') / 'closed.json'])
        for path, facts in ((root_output / ('alias-' + host + '1/alias.o'), alias['alias_object']),
                (root_output / ('alias-' + host + '1') / ('checked-alias.' + suffix), alias['programs']['checked'])):
            value = common.read(path)
            if identity(value) != facts: raise ValueError('A reused complete alias product differs')
            retained[path] = value
        captured.update(retained)
        stdout = data
        (output / 'root-prerequisite-stdout.bin').write_bytes(stdout)
        (output / 'root-prerequisite-stderr.bin').write_bytes(b'')
        receipt['commands'].append({'label': 'root-prerequisite', 'mode': 'reuse-complete-accepted-root',
            'output': str(root_output), 'exit_code': 0, 'seconds': time.monotonic() - before,
            'stdout': identity(stdout), 'stderr': identity(b'')})
        receipt['root_prerequisite_reused'] = True
        guard()

    try:
        if args.root_prerequisite:
            reuse_root()
        else:
            run('root-prerequisite', artifact / 'replay-native-source-root.py', ['--output', root_output])
        root_closed_bytes = (root_output / 'closed.json').read_bytes()
        root_closed = json.loads(root_closed_bytes)
        if root_closed['status'] != 'pass' or root_closed['host'] != host:
            raise ValueError('Complete root prerequisite did not pass')
        if root_closed['objects'] != root_proof['hosts'][host]['objects'] or root_closed['checked_images'] != root_proof['hosts'][host]['checked_images']:
            raise ValueError('Complete root prerequisite products differ')
        (output / 'root-prerequisite.json').write_text(json.dumps({'output': str(root_output),
            'closed': identity(root_closed_bytes)}, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
        sources = output / 'source1'
        sources.mkdir()
        for name, facts in root_proof['prototype_sources'].items():
            data = (root_output / ('products-' + host + '1/source') / name).read_bytes()
            if identity(data) != facts: raise ValueError('Root prerequisite source differs: ' + name)
            (sources / name).write_bytes(data)
        apply_sources(patch_bytes, root, sources, proof['prototype_sources'])
        if {path.name: identity(path.read_bytes()) for path in sources.iterdir()} != proof['prototype_sources']:
            raise ValueError('Complete sixteen-source copy differs')
        (output / 'source-controls.json').write_text(json.dumps({
            'status': 'pass', 'source_revision': proof['source_revision'],
            'source_snapshot_sha256': proof['source_snapshot_sha256'], 'source_inputs': proof['source_inputs']},
            indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
        run('build', output / 'build-source-file.py', [output / ('products-' + host + '1')])
        run('file-contracts', output / 'run-source-file-contracts.py', [])
        builds = json.loads((output / ('products-' + host + '1') / 'closed.json').read_bytes())
        contracts = json.loads((output / ('file-contracts-' + host + '1') / 'closed.json').read_bytes())
        if builds['status'] != 'pass' or contracts['status'] != 'pass':
            raise ValueError('A file-owner child did not pass')
        if builds['sources'] != proof['prototype_sources']:
            raise ValueError('Complete build source copy differs')
        if (contracts['calls'], contracts['positive_calls'], contracts['negative_calls']) != ((106, 44, 62) if windows else (126, 58, 68)):
            raise ValueError('Complete file-owner runtime selections differ')
        expected = proof['hosts'][host]
        if builds['objects'] != expected['objects'] or builds['checked_image'] != expected['checked_image']:
            raise ValueError('Complete checked file-owner products differ')
        for name, facts in builds['objects'].items():
            if identity(common.read(output / ('products-' + host + '1/objects') / name)) != facts:
                raise ValueError('Checked object differs: ' + name)
        if identity(common.read(output / ('products-' + host + '1') / ('checked-source-file.' + suffix))) != builds['checked_image']:
            raise ValueError('Checked program differs')
        for name in common.SEED_FILES:
            if common.read(root / name) != common.git_bytes('HEAD', name): raise ValueError('Installed seed changed: ' + name)
        guard()
        receipt.update(status='pass', file_calls=contracts['calls'], file_positive_calls=contracts['positive_calls'],
            file_negative_calls=contracts['negative_calls'], root_prerequisite=identity(root_closed_bytes),
            root_api_calls=root_closed['source_root_calls'], source_profile_calls=root_closed['source_profile_calls'],
            preceding_api_calls=root_closed['retained_api_calls'] + root_closed['missing_unc_api_calls'] + root_closed['resolved_parent_api_calls'],
            legacy_passed_methods=root_closed['legacy_passed_methods'], legacy_declared_skips=root_closed['legacy_declared_skips'],
            alias_passed_methods=root_closed['alias_selected_methods'], normal_source_inputs_unchanged=99,
            installed_root_seed_files_unchanged=15, complete_source_copies_equal=True,
            objects=builds['objects'], checked_image=builds['checked_image'])
    except BaseException as error:
        receipt.update(status='fail', error=repr(error))
        traceback.print_exc()
    receipt['seconds'] = time.monotonic() - started
    (output / 'closed.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    print(json.dumps({key: receipt.get(key) for key in ('status', 'host', 'file_calls', 'file_positive_calls',
        'file_negative_calls', 'seconds', 'error')}))
    return 0 if receipt['status'] == 'pass' else 1


if __name__ == '__main__':
    try:
        result = main()
    except Exception as error:
        traceback.print_exc()
        if created_output is not None and not (created_output / 'closed.json').exists():
            (created_output / 'closed.json').write_text(json.dumps({'schema': 'cupid.native-source-file-replay.v1',
                'status': 'fail', 'stage': 'preflight', 'error': repr(error)}, indent=2) + '\n', encoding='utf-8', newline='\n')
        result = 1
    raise SystemExit(result)

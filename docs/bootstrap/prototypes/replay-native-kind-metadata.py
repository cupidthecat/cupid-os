"""Replay initial leaf kind/metadata reuse against every existing contract."""
import argparse
import gzip
import hashlib
import importlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time
import traceback


def identity(data):
    return {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    artifact = Path(__file__).resolve().parent
    root = artifact.parents[2]
    output = args.output.resolve()
    if output.parent != root / 'build' or output.exists() or output.is_symlink():
        raise ValueError('Use a fresh direct child of the repository build directory')
    proof_path = artifact.parent / 'evidence/native-kind-metadata-20261011.json'
    proof_bytes = proof_path.read_bytes()
    proof = json.loads(proof_bytes)
    if proof['status'] not in ('paired-acceptance-repository-replay-pending', 'pass') or proof['complete_api_calls'] != 3630:
        raise ValueError('The complete shared-API acceptance differs')
    if identity(Path(__file__).read_bytes()) != proof['replay_runner']:
        raise ValueError('The reviewed replay runner differs')
    captured = {proof_path: proof_bytes, Path(__file__): Path(__file__).read_bytes()}
    archive_path = artifact.parent / 'evidence/native-kind-metadata-originals-20261011.json.gz'
    archive_bytes = archive_path.read_bytes()
    archive_raw = gzip.decompress(archive_bytes)
    if identity(archive_bytes) != proof['original_receipts_archive'] or identity(archive_raw) != proof['original_receipts_uncompressed']:
        raise ValueError('The complete original archive differs')
    captured[archive_path] = archive_bytes
    originals = json.loads(archive_raw)
    patch_path = artifact / 'native-kind-metadata.patch'
    patch_bytes = patch_path.read_bytes()
    if identity(patch_bytes) != proof['patch']:
        raise ValueError('The combined source patch differs')
    captured[patch_path] = patch_bytes
    output.mkdir()
    helpers = artifact / 'native-kind-metadata'
    if len(proof['replay_helpers']) != 32 or {p.name for p in helpers.iterdir() if p.is_file()} != set(proof['replay_helpers']):
        raise ValueError('The complete helper inventory differs')
    for name, facts in proof['replay_helpers'].items():
        path = helpers / name
        data = path.read_bytes()
        if identity(data) != facts:
            raise ValueError('Replay helper differs: ' + name)
        captured[path] = data
        (output / name).write_bytes(data)
        captured[output / name] = data
    sys.path.insert(0, str(output))
    common = importlib.import_module('consumer_audit_common')
    patcher = importlib.import_module('patch_sources')
    for section, revision in [('root_files', proof['checkpoint']), ('source_inputs', proof['source_revision'])]:
        for name, facts in proof[section].items():
            path = root / name
            data = common.read(path)
            if identity(data) != facts or data != common.git_bytes(revision, name):
                raise ValueError('Committed control differs: ' + name)
            if section == 'root_files' and data != common.git_bytes('HEAD', name):
                raise ValueError('Working root is not the accepted committed projection: ' + name)
            captured[path] = data
    for name, facts in proof['support_inputs'].items():
        path = root / name
        data = common.read(path)
        if identity(data) != facts:
            raise ValueError('Replay support differs: ' + name)
        captured[path] = data
    extraction = output / 'patch-sources'
    extraction.mkdir()
    patched = {name: row['identity'] for name, row in proof['patched_sources'].items()}
    patcher.apply_sources(patch_bytes, root, extraction, patched, proof['patched_sources'])
    source = output / 'common-source'
    source.mkdir()
    for name in patched:
        data = common.read(extraction / name)
        target = output / name if name in {'source-stage-capacity-contract.cc', 'source_parent_binding_contract.cc', 'retained_stream_group_contract.cc', 'path_observation_group_contract.cc', 'file_request_group_contract.cc', 'selection_path_group_contract.cc'} else source / name
        target.write_bytes(data)
        captured[extraction / name] = data
        captured[target] = data
    sys.path.insert(0, str(root))
    for name, row in proof['supplemental_sources'].items():
        if 'path' in row:
            path = root / row['path']
            data = common.read(path)
            captured[path] = data
        else:
            module = importlib.import_module(row['module'])
            data = getattr(module, row['constant']).encode('ascii')
            if Path(module.__file__) not in captured:
                raise ValueError('Supplemental module is not guarded')
        if identity(data) != row['identity']:
            raise ValueError('Supplemental source differs: ' + name)
        (source / name).write_bytes(data)
        captured[source / name] = data
    options_path = artifact / 'native-image-options.patch'
    options_bytes = common.read(options_path)
    options_proof_path = artifact.parent / 'evidence/native-image-options-20261010.json'
    options_proof_bytes = common.read(options_proof_path)
    if identity(options_bytes) != proof['options_patch'] or identity(options_proof_bytes) != proof['options_proof']:
        raise ValueError('The held option prerequisite differs')
    captured[options_path] = options_bytes
    captured[options_proof_path] = options_proof_bytes
    lines = options_bytes.decode('utf-8').splitlines(True)
    cursor, seen = 0, set()
    while cursor < len(lines):
        if lines[cursor] != '--- /dev/null\n' or not lines[cursor + 1].startswith('+++ b/'):
            raise ValueError('Unexpected options patch header')
        name = Path(lines[cursor + 1][6:].strip()).name
        match = re.fullmatch(r'@@ -0,0 \+1,(\d+) @@\n', lines[cursor + 2])
        if not match or name not in proof['options_support'] or name in seen:
            raise ValueError('Unexpected options source')
        count = int(match[1])
        body = lines[cursor + 3:cursor + 3 + count]
        if len(body) != count or any(not line.startswith('+') for line in body):
            raise ValueError('Incomplete options source')
        data = ''.join(line[1:] for line in body).encode('utf-8')
        if identity(data) != proof['options_support'][name]:
            raise ValueError('Complete options source differs')
        (source / name).write_bytes(data)
        captured[source / name] = data
        seen.add(name)
        cursor += 3 + count
    if seen != set(proof['options_support']) or {p.name: identity(common.read(p)) for p in source.iterdir()} != proof['core_sources']:
        raise ValueError('The complete shared source copy differs')
    records = {
        'source-controls.json': {k: proof[k] for k in ['source_revision', 'source_snapshot_sha256', 'source_inputs']},
        'preceding-checkpoint-controls.json': {'status': 'accepted-checkpoint', 'checkpoint': proof['checkpoint'], 'projected_files': proof['root_files']},
        'common-controls.json': {'status': 'prepared-not-tested', 'sources': proof['core_sources'], 'normal_source_inputs': proof['source_inputs'],
            'producer_sentinels': proof['limits']['producer_sentinels'], 'scope': proof['scope']}}
    for name, record in records.items():
        data = (json.dumps(record, indent=2, sort_keys=True) + '\n').encode('utf-8')
        (output / name).write_bytes(data)
        captured[output / name] = data
    from tools import bootstrap_toolchain as bootstrap
    windows = os.name == 'nt'
    host = 'windows' if windows else 'linux'
    seed = bootstrap.freeze_seed_inputs(root / ('bootstrap/seeds/i386-' + host + '/manifest.json'), output / 'seed')
    sentinels = proof['limits']['producer_sentinels']
    receipt = {'schema': 'cupid.kind-metadata-replay.v1', 'status': 'running', 'host': host,
        'proof': identity(proof_bytes), 'runner': identity(Path(__file__).read_bytes()), 'commands': [], 'child_receipts': {}}
    started = time.monotonic()

    def guard():
        bootstrap.require_live_seed_inputs(seed)
        for path, data in captured.items():
            if common.read(path) != data:
                raise ValueError('Input changed during replay: ' + str(path))

    def run(label, script):
        guard()
        before = time.monotonic()
        argv = [sys.executable, str(output / script)]
        result = subprocess.run(argv, cwd=root, env={**os.environ, **sentinels}, capture_output=True)
        for stream in ('stdout', 'stderr'):
            (output / (label + '-' + stream + '.bin')).write_bytes(getattr(result, stream))
        receipt['commands'].append({'label': label, 'argv': argv, 'exit_code': result.returncode,
            'seconds': time.monotonic() - before, 'stdout': identity(result.stdout), 'stderr': identity(result.stderr)})
        (output / 'running.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
        if result.returncode:
            raise ValueError('A complete replay step failed: ' + label)
        guard()

    steps = [('build', 'build-common-contracts.py'), ('aggregate', 'common-run-root-regressions.py'),
        ('required', 'common-retained-required-windows.py' if windows else 'common-retained-required-linux.py'),
        ('optional', 'common-retained-optional-windows.py' if windows else 'common-retained-optional-linux.py'),
        ('stage', 'common-run-source-stage-windows-third.py'), ('selection', 'common-run-source-selection.py'),
        ('bindings', 'common-run-input-binding.py'), ('requests', 'common-run-input-request-binding.py'),
        ('parent-build', 'build-common-parent-binding.py'), ('parents', 'common-run-parent-binding.py'),
        ('capacity-build', 'build-common-stage-capacity.py'), ('capacity', 'common-run-stage-capacity.py'), ('stream-group-build', 'build-retained-stream-group.py'), ('stream-group-contracts', 'run-retained-stream-group.py'), ('path-group-build', 'build-path-observation-group.py'), ('path-group-contracts', 'run-path-observation-group.py'), ('file-request-group-build', 'build-file-request-group.py'), ('file-request-group-contracts', 'run-file-request-group.py'), ('selection-path-group-build', 'build-selection-path-group.py'), ('selection-path-group-contracts', 'run-selection-path-group.py')]
    try:
        for label, script in steps:
            run(label, script)
        for label, row in originals['hosts'][host].items():
            folder = output / row['directory']
            data = common.read(folder / 'closed.json')
            record = json.loads(data)
            original = json.loads(row['receipt_text'])
            if record['status'] != 'pass':
                raise ValueError('A complete child receipt failed: ' + label)
            for key in ['objects', 'checked_images', 'object', 'checked_image', 'alias_object',
                        'calls', 'positive_calls', 'negative_calls', 'api_calls', 'selected_methods', 'passed_methods', 'declared_skips',
                        'collision_comparisons', 'stale_validation_rejections', 'publication_rejection_checks',
                        'quota_exhaustion_controls', 'rejection_clear_poison_checks', 'repeated_binding_checks']:
                if key in original and record.get(key) != original[key]:
                    raise ValueError('Complete accepted result differs: ' + label + ':' + key)
            if label == 'alias' and record['programs']['checked'] != original['programs']['checked']:
                raise ValueError('The complete checked alias program differs')
            receipt['child_receipts'][label] = identity(data)
            captured[folder / 'closed.json'] = data
        guard()
        receipt.update(status='pass', complete_api_calls=proof['original_hosts'][host]['complete_api_calls'],
            source_selection_group_api_calls=proof['original_hosts'][host]['source_selection_group_api_calls'],
            controlled_file_identities={str(path): identity(data) for path, data in captured.items()})
    except BaseException as error:
        receipt.update(status='fail', error=repr(error))
        traceback.print_exc()
    receipt['seconds'] = time.monotonic() - started
    (output / 'closed.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    print(json.dumps({k: receipt.get(k) for k in ['status', 'host', 'complete_api_calls', 'source_selection_group_api_calls', 'seconds', 'error']}))
    raise SystemExit(0 if receipt['status'] == 'pass' else 1)


if __name__ == '__main__':
    main()

"""Rebuild optional source-file owners and replay every accepted prerequisite."""
import argparse
import hashlib
import importlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import traceback

created_output = None
CASE_KEYS = ('role', 'label', 'mode', 'profile', 'expected_exit_code', 'expected_stdout',
             'distinct_missing_paths', 'tree_files', 'invalid', 'mutation', 'names_hex',
             'logical_hex', 'expected_kind', 'exit_code', 'stdout', 'stderr',
             'status', 'test', 'group')


def identity(data):
    return {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def case_plan(record):
    return [{key: row[key] for key in CASE_KEYS if key in row} for row in record['records']]


def main():
    global created_output
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    artifact = Path(__file__).resolve().parent
    root = artifact.parents[2]
    output = args.output.resolve()
    if output.parent != root / 'build' or output.exists() or output.is_symlink():
        raise ValueError('Use a fresh direct child of the repository build directory')
    output.mkdir()
    created_output = output
    proof_path = artifact.parent / 'evidence/native-source-optional-20261010.json'
    proof_bytes = proof_path.read_bytes()
    proof = json.loads(proof_bytes)
    if proof['status'] not in ('paired-acceptance-complete-repository-replay-pending', 'pass'):
        raise ValueError('Complete original paired acceptance is required')
    if proof['complete_current_api_calls'] != 1742 or proof['source_files'] != 17:
        raise ValueError('The complete accepted scope differs')
    if identity(Path(__file__).read_bytes()) != proof['replay_runner']:
        raise ValueError('The reviewed replay runner differs')
    captured = {proof_path: proof_bytes, Path(__file__): Path(__file__).read_bytes()}
    patch_path = artifact / 'native-source-optional.patch'
    patch_bytes = patch_path.read_bytes()
    if identity(patch_bytes) != proof['patch']:
        raise ValueError('The reviewed combined source patch differs')
    captured[patch_path] = patch_bytes
    original_path = artifact.parent / 'evidence/native-source-optional-originals-20261010.json.gz'
    original_bytes = original_path.read_bytes()
    if identity(original_bytes) != proof['original_receipts_archive']:
        raise ValueError('The complete original receipts differ')
    captured[original_path] = original_bytes
    helpers = artifact / 'native-source-optional'
    if set(proof['replay_helpers']) != {
            'consumer_audit_common.py', 'replay_guard.py', 'patch_sources.py',
            'build-root-regressions.py', 'run-source-root-contracts.py',
            'run-source-observer-contracts.py', 'run-missing-unc-contracts.py',
            'run-resolved-parent-contracts.py', 'run-retained-contracts.py',
            'run-legacy-contracts.py', 'run-alias-regressions.py',
            'build-source-optional.py', 'run-required-file-contracts.py',
            'run-optional-contracts.py', 'probe-python-optional-boundaries.py'}:
        raise ValueError('The complete replay helper inventory differs')
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
    heap_path = artifact.parent / 'evidence/hosted-heap-adoption-20261010.json'
    heap_bytes = common.read(heap_path)
    heap = json.loads(heap_bytes)
    if heap['status'] != 'pass' or heap['root_adoption']['status'] != 'pass':
        raise ValueError('Complete committed hosted heap adoption is required')
    captured[heap_path] = heap_bytes
    if len(proof['root_files']) != 20 or len(proof['source_inputs']) != 99:
        raise ValueError('The committed seed and normal input inventories differ')
    for name, facts in proof['root_files'].items():
        path = root / name
        data = common.read(path)
        if identity(data) != facts or data != common.git_bytes('HEAD', name):
            raise ValueError('Committed heap projection differs: ' + name)
        captured[path] = data
    for name, facts in proof['source_inputs'].items():
        path = root / name
        data = common.read(path)
        if identity(data) != facts or data != common.git_bytes(proof['source_revision'], name):
            raise ValueError('Named normal producer input differs: ' + name)
        captured[path] = data
    for name, facts in proof['support_inputs'].items():
        path = root / name
        data = common.read(path)
        if identity(data) != facts:
            raise ValueError('Replay support input differs: ' + name)
        captured[path] = data
    sources = output / 'source3'
    sources.mkdir()
    patcher.apply_sources(patch_bytes, root, sources, proof['prototype_sources'], proof['patched_sources'])
    sys.path.insert(0, str(root))
    for name, row in proof['supplemental_sources'].items():
        if 'path' in row:
            path = root / row['path']
            data = common.read(path)
            captured[path] = data
        else:
            module = importlib.import_module(row['module'])
            data = getattr(module, row['constant']).encode('ascii')
            path = Path(module.__file__)
            if path not in captured:
                raise ValueError('The supplemental caller module is not guarded')
        if identity(data) != row['identity'] or row['identity'] != proof['prototype_sources'][name]:
            raise ValueError('Supplemental source differs: ' + name)
        (sources / name).write_bytes(data)
    if {path.name: identity(common.read(path)) for path in sources.iterdir()} != proof['prototype_sources']:
        raise ValueError('The complete seventeen-source copy differs')
    for path in sources.iterdir():
        captured[path] = common.read(path)
    source_controls = {key: proof[key] for key in ('source_revision', 'source_snapshot_sha256', 'source_inputs')}
    controls = {'schema': 'cupid.native-source-optional-replay-controls.v1', 'status': 'pass',
                **source_controls, 'root_files': proof['root_files'], 'support_inputs': proof['support_inputs'],
                'sources': proof['prototype_sources'],
                'accepted_root_builds': {host: proof['original_hosts'][host]['root_build'] for host in ('linux', 'windows')}}
    for name, record in (('source-controls.json', source_controls), ('replay-controls.json', controls)):
        data = (json.dumps(record, indent=2, sort_keys=True) + '\n').encode('utf-8')
        (output / name).write_bytes(data)
        captured[output / name] = data
    from tools import bootstrap_toolchain as bootstrap
    windows = os.name == 'nt'
    host, suffix = ('windows', 'exe') if windows else ('linux', 'elf')
    seed = bootstrap.freeze_seed_inputs(root / ('bootstrap/seeds/i386-' + host + '/manifest.json'), output / 'seed')
    tool_identities = {name: identity(common.read(path)) for name, path in seed.tools.items()}
    expected = proof['original_hosts'][host]
    if tool_identities != expected['root_build']['actual_installed_tools']:
        raise ValueError('The complete execution cohort differs from original acceptance')
    for path in seed.tools.values():
        signature = common.read(path)[:4]
        if not (signature[:2] == b'MZ' if windows else signature == b'\x7fELF'):
            raise ValueError('Use the native checked host seed')
    sentinels = {name: 'forbidden-host-' + name for name in ('CC', 'CXX', 'CPP', 'AS', 'AR', 'LD', 'NM', 'OBJCOPY', 'NASM')}
    receipt = {'schema': 'cupid.native-source-optional-replay.v1', 'status': 'running', 'host': host,
               'runner': identity(Path(__file__).read_bytes()), 'proof': identity(proof_bytes),
               'controls': identity(captured[output / 'replay-controls.json']), 'commands': [],
               'producer_sentinels': sentinels, 'seed_manifest': identity(seed.manifest_bytes),
               'tools': tool_identities, 'source_revision': proof['source_revision'],
               'source_snapshot_sha256': proof['source_snapshot_sha256'], 'child_receipts': {}}
    started = time.monotonic()

    def guard():
        bootstrap.require_live_seed_inputs(seed)
        for path, data in captured.items():
            if common.read(path) != data:
                raise ValueError('Input changed during replay: ' + str(path))

    def run(label, script, script_args):
        guard()
        before = time.monotonic()
        result = subprocess.run([sys.executable, str(output / script), *map(str, script_args)], cwd=root,
                                env={**os.environ, **sentinels}, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        for stream in ('stdout', 'stderr'):
            (output / (label + '-' + stream + '.bin')).write_bytes(getattr(result, stream))
        receipt['commands'].append({'label': label, 'argv': [sys.executable, str(output / script), *map(str, script_args)],
                                    'exit_code': result.returncode, 'seconds': time.monotonic() - before,
                                    'stdout': identity(result.stdout), 'stderr': identity(result.stderr)})
        (output / 'running.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
        if result.returncode != 0:
            raise ValueError(receipt['commands'][-1])
        guard()

    def child(label, stem):
        path = output / (stem + '-' + host + '1') / 'closed.json'
        data = common.read(path)
        record = json.loads(data)
        if record['status'] != 'pass' or record['host'] != host:
            raise ValueError('A complete child acceptance failed: ' + label)
        receipt['child_receipts'][label] = identity(data)
        captured[path] = data
        return record, path.parent

    def complete_build(record, folder):
        if record['sources'] != proof['prototype_sources'] or record['producer_sentinels'] != sentinels:
            raise ValueError('A build source or producer contract differs')
        for name, facts in record['sources'].items():
            data = common.read(folder / 'source' / name)
            if identity(data) != facts or data != common.read(sources / name):
                raise ValueError('A complete built source differs: ' + name)
            captured[folder / 'source' / name] = data
        for name, facts in record['objects'].items():
            data = common.read(folder / 'objects' / name)
            if identity(data) != facts:
                raise ValueError('A complete checked object differs: ' + name)
            captured[folder / 'objects' / name] = data
        for command in record['commands']:
            if command['exit_code'] != 0:
                raise ValueError('A build command failed')
            for stream in ('stdout', 'stderr'):
                data = common.read(folder / (command['label'] + '-' + stream + '.bin'))
                if identity(data) != command[stream] or data != b'':
                    raise ValueError('An original build command stream differs')
                captured[folder / (command['label'] + '-' + stream + '.bin')] = data
        for role in ('native', 'checked'):
            if role + '_images' in record:
                images = record[role + '_images']
            else:
                images = {'source-file': record[role + '_image'], 'source-optional': record[role + '_optional_image']}
            for name, facts in images.items():
                path = folder / (role + '-' + name + '.' + suffix)
                data = common.read(path)
                if identity(data) != facts:
                    raise ValueError('A complete program differs: ' + name)
                captured[path] = data

    def complete_cases(label, record):
        if case_plan(record) != expected['case_plans'][label]:
            raise ValueError('The original complete case plan differs: ' + label)
        if 'calls' in record and record['calls'] != len(record['records']):
            raise ValueError('An API call is unaccounted for: ' + label)
        if label in ('legacy', 'alias'):
            if record['test_bodies_changed'] is not False or record['original_test_timeouts_changed'] is not False:
                raise ValueError('An original test body or deadline changed')
            if record['original_process_resource_limits_preserved'] is not True:
                raise ValueError('An original test resource bound changed')
            if any(row['status'] not in ('pass', 'skip') for row in record['records']):
                raise ValueError('An original method did not pass or declare its skip')
            return
        if record.get('producer_sentinels') != sentinels:
            raise ValueError('A runtime producer contract differs: ' + label)
        if record.get('timeout_seconds', 60) != 60:
            raise ValueError('An original runtime deadline changed')
        for row in record['records']:
            if row.get('status', 'pass') != 'pass' or row.get('error') is not None:
                raise ValueError('A runtime case failed: ' + label)
            if row['exit_code'] != row.get('expected_exit_code', 0) or row['stderr'] != '':
                raise ValueError('A runtime result differs: ' + label)
            if 'expected_stdout' in row and row['stdout'] != row['expected_stdout']:
                raise ValueError('A runtime stdout oracle differs: ' + label)
            if row['timeout_seconds'] != 60:
                raise ValueError('An original runtime row deadline changed')
            for stream in ('stdout', 'stderr'):
                if stream + '_identity' in row and identity(row[stream].encode('utf-8')) != row[stream + '_identity']:
                    raise ValueError('A runtime stream identity differs: ' + label)
            if not windows:
                for key, value in (('address_space_bytes', 33554432), ('descriptor_limit', 10240)):
                    if key in row and row[key] != value:
                        raise ValueError('An original runtime resource bound changed: ' + label)

    try:
        for label, script, script_args in (
                ('root-build', 'build-root-regressions.py', [output / ('root-products-' + host + '1')]),
                ('source-root', 'run-source-root-contracts.py', []),
                ('source-profile', 'run-source-observer-contracts.py', []),
                ('missing-unc', 'run-missing-unc-contracts.py', []),
                ('resolved-parent', 'run-resolved-parent-contracts.py', []),
                ('contracts', 'run-retained-contracts.py', []),
                ('legacy', 'run-legacy-contracts.py', ['1']),
                ('alias', 'run-alias-regressions.py', []),
                ('file-build', 'build-source-optional.py', [output / ('products-' + host + '1')]),
                ('required-file', 'run-required-file-contracts.py', []),
                ('optional-file', 'run-optional-contracts.py', []),
                ('python-boundaries', 'probe-python-optional-boundaries.py', [])):
            run(label, script, script_args)
        groups = {}
        folders = {}
        for label, stem in (
                ('root-build', 'root-products'), ('source-root', 'root-contracts'),
                ('source-profile', 'source-contracts'), ('missing-unc', 'missing-unc'),
                ('resolved-parent', 'resolved-parent'), ('contracts', 'contracts'),
                ('legacy', 'legacy'), ('alias', 'alias'), ('file-build', 'products'),
                ('required-file', 'file-contracts'), ('optional-file', 'optional-contracts'),
                ('python-boundaries', 'python-optional-boundaries')):
            groups[label], folders[label] = child(label, stem)
        root_build, file_build = groups['root-build'], groups['file-build']
        for label, record in (('root-build', root_build), ('file-build', file_build)):
            complete_build(record, folders[label])
        if root_build['objects'] != expected['root_build']['objects'] or root_build['checked_images'] != expected['root_build']['checked_images']:
            raise ValueError('The original complete root products differ')
        if file_build['objects'] != expected['file_build']['objects']:
            raise ValueError('The original complete file objects differ')
        for key in ('checked_image', 'checked_optional_image'):
            if file_build[key] != expected['file_build'][key]:
                raise ValueError('An original complete file program differs')
        if common.read(folders['root-build'] / 'objects/cupidbuild_host.o') != common.read(folders['file-build'] / 'objects/cupidbuild_host.o'):
            raise ValueError('The two whole changed host objects differ')
        for label, record in groups.items():
            if label not in ('root-build', 'file-build', 'python-boundaries'):
                complete_cases(label, record)
        required, optional = groups['required-file'], groups['optional-file']
        if (required['calls'], required['positive_calls'], required['negative_calls']) != ((106, 44, 62) if windows else (126, 58, 68)):
            raise ValueError('The complete required-file selection differs')
        if (optional['calls'], optional['positive_calls'], optional['negative_calls']) != ((188, 84, 104) if windows else (194, 86, 108)):
            raise ValueError('The complete optional-file selection differs')
        if len(required['actual_python_oracles']) != (72 if windows else 86) or len(optional['actual_python_oracles']) != (146 if windows else 148):
            raise ValueError('An actual Python oracle is missing')
        preceding = sum(groups[label]['calls'] for label in ('source-root', 'source-profile', 'missing-unc', 'resolved-parent', 'contracts'))
        if preceding != (526 if windows else 602):
            raise ValueError('The complete preceding API selection differs')
        legacy, alias = groups['legacy'], groups['alias']
        if (legacy['selected_methods'], legacy['passed_methods'], legacy['declared_skips']) != ((176, 167, 9) if windows else (176, 173, 3)):
            raise ValueError('The complete legacy selection differs')
        if (alias['selected_methods'], alias['passed_methods'], alias['declared_skips']) != (24, 24, 0):
            raise ValueError('The complete alias selection differs')
        if alias['alias_object'] != expected['alias_object'] or alias['programs']['checked'] != expected['alias_checked_image']:
            raise ValueError('An original whole alias product differs')
        for path, facts in ((folders['alias'] / 'alias.o', alias['alias_object']),
                            (folders['alias'] / ('checked-alias.' + suffix), alias['programs']['checked'])):
            data = common.read(path)
            if identity(data) != facts:
                raise ValueError('A complete alias product changed')
            captured[path] = data
        boundaries = groups['python-boundaries']
        if boundaries['calls'] != 4 or boundaries['build'] != identity(captured[folders['file-build'] / 'closed.json']):
            raise ValueError('The complete Python boundary diagnostic differs')
        for row in boundaries['records']:
            if row['exit_code'] != 2 or row['stdout'] != 'reject' or row['stderr'] != '' or row['timeout_seconds'] != 60:
                raise ValueError('A Python boundary owner result differs')
            if row['python_classification'] != ('reject' if not windows and row['label'] == 'parent-file' else 'missing'):
                raise ValueError('An actual Python boundary result differs')
            if not windows and (row['linux_address_space_bytes'], row['linux_descriptor_limit']) != (33554432, 10240):
                raise ValueError('A Python boundary resource limit changed')
        objects = {'root/' + name: facts for name, facts in root_build['objects'].items()}
        objects.update({'file/' + name: facts for name, facts in file_build['objects'].items() if name != 'cupidbuild_host.o'})
        objects['alias.o'] = alias['alias_object']
        images = {'root/' + name: facts for name, facts in root_build['checked_images'].items()}
        images.update({'file/source-file': file_build['checked_image'], 'file/source-optional': file_build['checked_optional_image'],
                       'alias': alias['programs']['checked']})
        strict_count = sum(row['label'].startswith('strict-') for build in (root_build, file_build, alias) for row in build['commands'])
        if len(objects) != (21 if windows else 16) or len(images) != 10 or strict_count != (32 if windows else 27):
            raise ValueError('The complete checked product or strict-check inventory differs')
        guard()
        receipt.update(status='pass', preceding_api_calls=preceding, required_api_calls=required['calls'],
                       optional_api_calls=optional['calls'], complete_current_api_calls=preceding + required['calls'] + optional['calls'],
                       optional_positive_calls=optional['positive_calls'], optional_negative_calls=optional['negative_calls'],
                       required_python_oracles=len(required['actual_python_oracles']), optional_python_oracles=len(optional['actual_python_oracles']),
                       python_boundary_diagnostic_calls=4, legacy_selected_methods=legacy['selected_methods'],
                       legacy_passed_methods=legacy['passed_methods'], legacy_declared_skips=legacy['declared_skips'],
                       alias_passed_methods=24, strict_check_executions=strict_count, whole_checked_objects=len(objects),
                       whole_checked_programs=len(images), objects=objects, checked_images=images,
                       normal_source_inputs_unchanged=99, guarded_committed_root_files=20,
                       complete_source_copies_equal=True, complete_original_products_equal=True)
    except BaseException as error:
        receipt.update(status='fail', error=repr(error))
        traceback.print_exc()
    receipt['seconds'] = time.monotonic() - started
    (output / 'closed.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    print(json.dumps({key: receipt.get(key) for key in ('status', 'host', 'complete_current_api_calls', 'seconds', 'error')}))
    return 0 if receipt['status'] == 'pass' else 1


if __name__ == '__main__':
    try:
        result = main()
    except Exception as error:
        traceback.print_exc()
        if created_output is not None and not (created_output / 'closed.json').exists():
            (created_output / 'closed.json').write_text(json.dumps({'schema': 'cupid.native-source-optional-replay.v1',
                'status': 'fail', 'stage': 'preflight', 'error': repr(error)}, indent=2) + '\n', encoding='utf-8', newline='\n')
        result = 1
    raise SystemExit(result)

"""Rebuild and replay source observer name profiles and all existing observer contracts."""
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


def apply_sources(patch_bytes, root, destination, expected):
    """Apply only the six reviewed files, checking each context and line count."""
    allowed = {
        'b/toolchain/cupidbuild_host.cc': 'toolchain/cupidbuild_host.cc',
        'b/toolchain/cupidbuild_host.h': 'toolchain/cupidbuild_host.h',
        'b/toolchain/prototypes/native_observer_paths/retained_observer_contract.cc': None,
        'b/toolchain/prototypes/native_observer_paths/observer_missing_unc_contract.cc': None,
        'b/toolchain/prototypes/native_observer_paths/unc_resolved_parent_contract.cc': None,
        'b/toolchain/prototypes/native_source_observer/source_observer_contract.cc': None,
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
    args = arguments.parse_args()
    artifact = Path(__file__).resolve().parent
    root = artifact.parents[2]
    output = args.output.resolve()
    if output.parent != root / 'build' or output.exists():
        raise ValueError('Use a fresh direct child of the repository build directory')
    output.mkdir(parents=True)
    created_output = output
    proof_path = artifact.parent / 'evidence/native-source-observer-20261010.json'
    proof_bytes = proof_path.read_bytes()
    proof = json.loads(proof_bytes)
    if proof['status'] != 'pass':
        raise ValueError('The complete original acceptance did not pass')
    patch_bytes = (artifact / 'native-source-observer.patch').read_bytes()
    if identity(patch_bytes) != proof['artifact_patch']:
        raise ValueError('Held source patch differs from the reviewed evidence')
    for name, facts in proof['replay_helpers'].items():
        data = (artifact / 'native-source-observer' / name).read_bytes()
        if identity(data) != facts:
            raise ValueError('Replay helper differs: ' + name)
        (output / name).write_bytes(data)
    spec = importlib.util.spec_from_file_location('consumer_audit_common', output / 'consumer_audit_common.py')
    common = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(common)
    captured = {proof_path: proof_bytes, artifact / 'native-source-observer.patch': patch_bytes}
    for name, facts in proof['source_inputs'].items():
        path = root / name
        data = common.read(path)
        if identity(data) != facts or data != common.git_bytes(proof['source_revision'], name):
            raise ValueError('Committed normal input differs: ' + name)
        captured[path] = data
    if len(proof['source_inputs']) != 99:
        raise ValueError('Normal producer inventory is incomplete')
    for name, facts in {**proof['legacy_test_sources'], **proof['support_inputs']}.items():
        path = root / name
        data = common.read(path)
        if identity(data) != facts:
            raise ValueError('Original legacy test source differs: ' + name)
        captured[path] = data
    for name in common.SEED_FILES:
        path = root / name
        data = common.read(path)
        if data != common.git_bytes('HEAD', name):
            raise ValueError('Installed seed differs from the current commit: ' + name)
        captured[path] = data
    sources = output / 'source'
    sources.mkdir()
    apply_sources(patch_bytes, root, sources, proof['prototype_sources'])
    for name in ('path_encoding.cc', 'path_encoding.h'):
        shutil.copyfile(root / 'toolchain' / name, sources / name)
        if identity((sources / name).read_bytes()) != proof['prototype_sources'][name]:
            raise ValueError('Path codec differs from the held source')
    (output / 'source-controls.json').write_text(json.dumps({
        'status': 'pass', 'source_revision': proof['source_revision'],
        'source_snapshot_sha256': proof['source_snapshot_sha256'], 'source_inputs': proof['source_inputs']},
        indent=2, sort_keys=True) + '\n', encoding='utf-8', newline='\n')
    sys.path.insert(0, str(root))
    from tools import bootstrap_toolchain as bootstrap
    windows = os.name == 'nt'
    host = 'windows' if windows else 'linux'
    manifest = root / ('bootstrap/seeds/i386-' + host + '/manifest.json')
    seed = bootstrap.freeze_seed_inputs(manifest, output / 'seed')
    for path in seed.tools.values():
        signature = path.read_bytes()[:4]
        if not (signature[:2] == b'MZ' if windows else signature == b'\x7fELF'):
            raise ValueError('Use a recognized native host seed manifest')
    (output / 'checked-tools.json').write_text(json.dumps({name: str(seed.tools[name]) for name in
        ('cupidc', 'cupidasm', 'cupidld', 'cupiddis')}, indent=2, sort_keys=True) + '\n',
        encoding='utf-8', newline='\n')
    sentinels = {name: 'forbidden-host-' + name for name in ('CC', 'CXX', 'CPP', 'AS', 'AR', 'LD', 'NM', 'OBJCOPY', 'NASM')}
    receipt = {'schema': 'cupid.native-source-observer-replay.v1', 'status': 'running', 'host': host,
        'runner': identity(Path(__file__).read_bytes()), 'commands': [], 'producer_sentinels': sentinels,
        'seed_manifest': identity(seed.manifest_bytes), 'artifact_patch': identity(patch_bytes),
        'tools': {name: identity(path.read_bytes()) for name, path in seed.tools.items()},
        'source_revision': proof['source_revision'], 'source_snapshot_sha256': proof['source_snapshot_sha256']}
    started = time.monotonic()

    def guard():
        bootstrap.require_live_seed_inputs(seed)
        for path, data in captured.items():
            if common.read(path) != data:
                raise ValueError('Input changed during replay: ' + str(path))

    try:
        for label, script, script_args in (
            ('build', 'build-observer-paths.py', [output / ('products-' + host + '1')]),
            ('source-profile', 'run-source-observer-contracts.py', []),
            ('missing-unc', 'run-missing-unc-contracts.py', []),
            ('resolved-parent', 'run-resolved-parent-contracts.py', []),
            ('contracts', 'run-retained-contracts.py', []),
            ('legacy', 'run-legacy-contracts.py', ['1']),
            ('alias', 'run-alias-regressions.py', [])):
            guard()
            before = time.monotonic()
            result = subprocess.run([sys.executable, str(output / script), *map(str, script_args)], cwd=root,
                env={**os.environ, **sentinels}, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            (output / (label + '-stdout.bin')).write_bytes(result.stdout)
            (output / (label + '-stderr.bin')).write_bytes(result.stderr)
            record = {'label': label, 'exit_code': result.returncode, 'seconds': time.monotonic() - before,
                'stdout': identity(result.stdout), 'stderr': identity(result.stderr)}
            receipt['commands'].append(record)
            (output / 'running.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n',
                encoding='utf-8', newline='\n')
            if result.returncode != 0:
                raise ValueError(record)
            guard()
        builds = json.loads((output / ('products-' + host + '1') / 'closed.json').read_bytes())
        source_profile = json.loads((output / ('source-contracts-' + host + '1') / 'closed.json').read_bytes())
        contracts = json.loads((output / ('contracts-' + host + '1') / 'closed.json').read_bytes())
        legacy = json.loads((output / ('legacy-' + host + '1') / 'closed.json').read_bytes())
        missing = json.loads((output / ('missing-unc-' + host + '1') / 'closed.json').read_bytes())
        resolved = json.loads((output / ('resolved-parent-' + host + '1') / 'closed.json').read_bytes())
        alias = json.loads((output / ('alias-' + host + '1') / 'closed.json').read_bytes())
        if any(record['status'] != 'pass' for record in (builds, source_profile, contracts, missing, resolved, legacy, alias)):
            raise ValueError('A child receipt did not close successfully')
        if builds['sources'] != proof['prototype_sources']:
            raise ValueError('The complete ten-source copy differs')
        if contracts['calls'] != (92 if windows else 86) or missing['calls'] != (104 if windows else 64) or resolved['calls'] != (22 if windows else 20) or legacy['selected_methods'] != 176:
            raise ValueError('The complete runtime selections differ')
        if alias['selected_methods'] != 24 or alias['passed_methods'] != 24 or alias['declared_skips'] != 0:
            raise ValueError('The complete twelve-method alias selections differ')
        if (source_profile['calls'], source_profile['positive_calls'], source_profile['negative_calls']) != ((226, 132, 94) if windows else (338, 184, 154)):
            raise ValueError('The complete source-profile calls differ')
        if builds['objects'] != proof['hosts'][host]['objects'] or builds['checked_images'] != proof['hosts'][host]['checked_images']:
            raise ValueError('The complete checked object or program inventory differs')
        for name, facts in builds['objects'].items():
            if identity(common.read(output / ('products-' + host + '1/objects') / name)) != facts:
                raise ValueError('A complete checked object differs: ' + name)
        for role, facts in builds['checked_images'].items():
            if identity(common.read(output / ('products-' + host + '1') / ('checked-' + role + ('.exe' if windows else '.elf')))) != facts:
                raise ValueError('A complete checked program differs: ' + role)
        for name in common.SEED_FILES:
            if common.read(root / name) != common.git_bytes('HEAD', name):
                raise ValueError('Installed seed changed in the current commit: ' + name)
        guard()
        receipt.update(status='pass', source_profile_calls=source_profile['calls'],
            source_profile_positive_calls=source_profile['positive_calls'], source_profile_negative_calls=source_profile['negative_calls'],
            retained_api_calls=contracts['calls'],
            missing_unc_api_calls=missing['calls'], resolved_parent_api_calls=resolved['calls'], alias_selected_methods=alias['selected_methods'],
            legacy_selected_methods=legacy['selected_methods'], legacy_passed_methods=legacy['passed_methods'],
            legacy_declared_skips=legacy['declared_skips'], normal_source_inputs_unchanged=99,
            installed_root_seed_files_unchanged=15, complete_source_copies_equal=True,
            objects=builds['objects'], checked_images=builds['checked_images'])
    except BaseException as error:
        receipt.update(status='fail', error=repr(error))
        traceback.print_exc()
    receipt['seconds'] = time.monotonic() - started
    (output / 'closed.json').write_text(json.dumps(receipt, indent=2, sort_keys=True) + '\n',
        encoding='utf-8', newline='\n')
    print(json.dumps({key: receipt.get(key) for key in ('status', 'host', 'source_profile_calls', 'retained_api_calls',
        'missing_unc_api_calls', 'resolved_parent_api_calls', 'legacy_selected_methods', 'legacy_passed_methods', 'legacy_declared_skips', 'seconds', 'error')}))
    return 0 if receipt['status'] == 'pass' else 1


if __name__ == '__main__':
    try:
        result = main()
    except Exception as error:
        traceback.print_exc()
        if created_output is not None and not (created_output / 'closed.json').exists():
            (created_output / 'closed.json').write_text(json.dumps({'schema': 'cupid.native-source-observer-replay.v1',
                'status': 'fail', 'stage': 'preflight', 'error': repr(error)}, indent=2) + '\n',
                encoding='utf-8', newline='\n')
        result = 1
    raise SystemExit(result)

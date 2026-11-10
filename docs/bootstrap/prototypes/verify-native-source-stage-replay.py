"""Reread every checked stage-replay product, including the alias fixture."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path


def identity(data):
    return {'size': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def check_alias(folder, record, suffix):
    if identity((folder / 'alias.o').read_bytes()) != record['alias_object']:
        raise ValueError('Complete alias object differs')
    for role, facts in record['programs'].items():
        if identity((folder / (role + '-alias.' + suffix)).read_bytes()) != facts:
            raise ValueError('Complete alias program differs: ' + role)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--replay', type=Path, required=True)
    args = parser.parse_args()
    artifact = Path(__file__).resolve().parent
    root = artifact.parents[2]
    folder = args.replay.resolve()
    if folder.parent != root / 'build' or not folder.is_dir():
        raise ValueError('Use a completed direct child of the repository build directory')
    proof_path = artifact.parent / 'evidence/native-source-stage-20261010.json'
    proof_bytes = proof_path.read_bytes()
    proof = json.loads(proof_bytes)
    archive_bytes = (artifact.parent / 'evidence/native-source-stage-originals-20261010.json.gz').read_bytes()
    raw_archive = gzip.decompress(archive_bytes)
    if identity(archive_bytes) != proof['original_receipts_archive'] or identity(raw_archive) != proof['original_receipts_uncompressed']:
        raise ValueError('Complete original archive differs')
    originals = json.loads(raw_archive)['hosts']
    closed = json.loads((folder / 'closed.json').read_bytes())
    accepted_proofs = [identity(proof_bytes)]
    if 'accepted_replay_proof' in proof:
        replay_archive = (artifact.parent / 'evidence/native-source-stage-replays-20261010.json.gz').read_bytes()
        replay_raw = gzip.decompress(replay_archive)
        if identity(replay_archive) != proof['replay_receipts_archive'] or identity(replay_raw) != proof['replay_receipts_uncompressed']:
            raise ValueError('Complete replay archive differs')
        accepted_text = json.loads(replay_raw)['accepted_replay_proof_text'].encode('utf-8')
        if identity(accepted_text) != proof['accepted_replay_proof']:
            raise ValueError('Original accepted replay proof differs')
        accepted_proofs.append(proof['accepted_replay_proof'])
    if closed['status'] != 'pass' or closed['proof'] not in accepted_proofs or closed['runner'] != proof['replay_runner']:
        raise ValueError('Completed replay does not match this acceptance')
    host = closed['host']
    suffix = 'exe' if host == 'windows' else 'elf'
    expected = originals[host]
    for name, facts in closed['controlled_file_identities'].items():
        if Path(name).resolve() == proof_path.resolve() and facts == proof.get('accepted_replay_proof'):
            continue
        if identity(Path(name).read_bytes()) != facts:
            raise ValueError('Complete controlled file differs: ' + name)
    stems = {'root_build': 'root-products', 'source-root': 'root-contracts',
             'source-profile': 'source-contracts', 'missing-unc': 'missing-unc',
             'resolved-parent': 'resolved-parent', 'contracts': 'contracts',
             'legacy': 'legacy', 'alias': 'alias', 'build': 'stage-products',
             'required': 'file-contracts', 'optional': 'optional-contracts',
             'stage': 'stage-contracts', 'capacity_build': 'stage-capacity-products',
             'capacity': 'stage-capacity-contracts'}
    if set(closed['child_receipts']) != set(stems) or len(closed['commands']) != 14:
        raise ValueError('Complete child inventory differs')
    strict = 0
    object_map = {}
    checked_programs = 0
    api_calls = 0
    for label, stem in stems.items():
        child_folder = folder / (stem + '-' + host + '1')
        data = (child_folder / 'closed.json').read_bytes()
        record = json.loads(data)
        if identity(data) != closed['child_receipts'][label] or record['status'] != 'pass':
            raise ValueError('Complete child receipt differs: ' + label)
        if 'calls' in record:
            if record['calls'] != expected[label]['calls'] or record['calls'] != len(record['records']):
                raise ValueError('Complete API selection differs: ' + label)
            api_calls += record['calls']
        if label in ('root_build', 'build'):
            if record['objects'] != expected[label]['objects'] or record['checked_images'] != expected[label]['checked_images']:
                raise ValueError('Complete checked products differ: ' + label)
            for name, facts in record['objects'].items():
                if identity((child_folder / 'objects' / name).read_bytes()) != facts:
                    raise ValueError('Complete checked object differs: ' + name)
            object_map.update(record['objects'])
            for role in ('native', 'checked'):
                for name, facts in record[role + '_images'].items():
                    if identity((child_folder / (role + '-' + name + '.' + suffix)).read_bytes()) != facts:
                        raise ValueError('Complete program differs: ' + role + '-' + name)
            checked_programs += len(record['checked_images'])
        elif label == 'capacity_build':
            if record['object'] != expected[label]['object'] or record['checked_image'] != expected[label]['checked_image']:
                raise ValueError('Complete capacity products differ')
            if identity((child_folder / 'stage-capacity.o').read_bytes()) != record['object']:
                raise ValueError('Complete capacity object differs')
            for role in ('native', 'checked'):
                if identity((child_folder / (role + '-stage-capacity.' + suffix)).read_bytes()) != record[role + '_image']:
                    raise ValueError('Complete capacity program differs: ' + role)
            object_map['stage-capacity.o'] = record['object']
            checked_programs += 1
        elif label == 'alias':
            if record['alias_object'] != expected[label]['alias_object'] or record['programs']['checked'] != expected[label]['programs']['checked']:
                raise ValueError('Complete checked alias products differ')
            check_alias(child_folder, record, suffix)
            object_map['alias.o'] = record['alias_object']
            checked_programs += 1
        else:
            continue
        for command in record['commands']:
            if command['exit_code'] != 0:
                raise ValueError('A complete build command failed')
            for stream in ('stdout', 'stderr'):
                data = (child_folder / (command['label'] + '-' + stream + '.bin')).read_bytes()
                if identity(data) != command[stream] or data != b'':
                    raise ValueError('Complete command stream differs')
            if command['label'].startswith('strict-'):
                if not all(flag in command['argv'] for flag in ('--require-known', '--require-local-targets', '--require-code-anchors')):
                    raise ValueError('A strict flag is missing')
                strict += 1
    if api_calls != closed['complete_api_calls'] or api_calls != (980 if host == 'windows' else 1074):
        raise ValueError('Complete API count differs')
    print(json.dumps({'schema': 'cupid.native-source-stage-replay-verification.v1',
                      'status': 'pass', 'host': host, 'complete_api_calls': api_calls,
                      'strict_check_executions': strict, 'whole_checked_objects': len(object_map),
                      'whole_checked_programs': checked_programs,
                      'alias_object': object_map['alias.o']}))


if __name__ == '__main__':
    main()
